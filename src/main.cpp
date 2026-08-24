// =========================================================================
// FastRelax — controlador da cadeira
//
// O ESP32 entra na rede como cliente e passa a ser comandado pelo backend:
//   backend -> POST /start  liga o relé pela duração informada
//   backend -> POST /stop   desliga antes do previsto
//   ESP32   -> POST /chairs/heartbeat  avisa que está vivo e informa o IP
//
// A contagem do tempo é local. Isso é deliberado: se a rede cair depois do
// /start, o relé precisa desligar sozinho. O backend também conta, mas apenas
// para reconciliar o estado da sessão no banco — quem garante o desligamento
// físico é este firmware.
// =========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "config.h"

// Porta fixa: o backend monta a URL com 80 por padrão, então não há o que
// configurar nem o que informar no heartbeat.
WebServer server(80);

// ---- Estado da sessão ----
// TESTING é o acionamento manual do relé: comporta-se como RUNNING, mas fica
// separado para o log e o /status deixarem claro que não há sessão real.
enum SessionPhase { IDLE, STARTING, RUNNING, TESTING };

SessionPhase phase = IDLE;
unsigned long sessionStartMs = 0;   // millis() no instante do comando
unsigned long startDelayMs = 0;     // espera antes de acionar o relé
unsigned long durationMs = 0;       // tempo de relé ligado
long currentSessionId = -1;         // id da sessão no backend, para log

unsigned long lastHeartbeatMs = 0;
unsigned long lastWifiAttemptMs = 0;

// Enquanto o backend não reconhecer o MAC, o banner de cadastro é reimpresso a
// cada heartbeat — quem está com o monitor serial aberto não precisa procurar
// a informação no meio do log.
bool registeredInBackend = false;

// =========================================================================
// RELÉ
// =========================================================================

void setRelay(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}

// =========================================================================
// IDENTIFICAÇÃO
// =========================================================================

/**
 * Dados necessários para cadastrar a cadeira no painel.
 *
 * O MAC é a identidade do dispositivo no backend e não muda; o IP é só o
 * endereço atual e o heartbeat o mantém atualizado sozinho.
 */
void printIdentity(const char* header) {
  Serial.println();
  Serial.println("==========================================================");
  Serial.printf("  %s\n", header);
  Serial.println("==========================================================");
  Serial.printf("  MAC address : %s\n", WiFi.macAddress().c_str());
  Serial.printf("  IP na rede  : %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("  Porta HTTP  : 80\n");
  Serial.printf("  Hostname    : %s\n", DEVICE_HOSTNAME);
  Serial.println("----------------------------------------------------------");
  Serial.println("  Cadastre o MAC acima em Painel > Cadeiras > Nova cadeira");
  Serial.println("==========================================================");
  Serial.println();
}

void endSession(const char* reason) {
  setRelay(false);
  phase = IDLE;
  Serial.printf("[sessao %ld] encerrada: %s\n", currentSessionId, reason);
  currentSessionId = -1;
}

// =========================================================================
// JSON
//
// Os corpos trocados aqui têm três campos numéricos no máximo, então extrair
// na mão evita uma dependência externa. Aceita apenas inteiros — que é tudo
// que o backend envia.
// =========================================================================

long readJsonNumber(const String& body, const char* key, long fallback) {
  String needle = "\"" + String(key) + "\"";
  int keyAt = body.indexOf(needle);
  if (keyAt < 0) {
    return fallback;
  }

  int colonAt = body.indexOf(':', keyAt + needle.length());
  if (colonAt < 0) {
    return fallback;
  }

  // Pula espaços e aspas até o primeiro caractere do valor.
  int cursor = colonAt + 1;
  while (cursor < (int)body.length() &&
         (body[cursor] == ' ' || body[cursor] == '"')) {
    cursor++;
  }

  bool negative = cursor < (int)body.length() && body[cursor] == '-';
  if (negative) {
    cursor++;
  }

  long value = 0;
  bool sawDigit = false;
  while (cursor < (int)body.length() && isDigit(body[cursor])) {
    value = value * 10 + (body[cursor] - '0');
    sawDigit = true;
    cursor++;
  }

  if (!sawDigit) {
    return fallback;
  }
  return negative ? -value : value;
}

// =========================================================================
// AUTENTICAÇÃO
// =========================================================================

/** Todo comando do backend precisa trazer o token combinado. */
bool authorized() {
  if (!server.hasHeader("X-Device-Token")) {
    return false;
  }
  return server.header("X-Device-Token") == String(DEVICE_TOKEN);
}

void sendJson(int code, const String& json) {
  server.send(code, "application/json", json);
}

// =========================================================================
// ROTAS CHAMADAS PELO BACKEND
// =========================================================================

/**
 * Inicia a sessão.
 *
 * Corpo: {"sessionId":42,"durationSeconds":300,"startDelaySeconds":5}
 *
 * Recusa quando já existe sessão em andamento: duas pessoas não usam a mesma
 * cadeira ao mesmo tempo, e sobrescrever em silêncio esconderia o problema de
 * quem já estava sentado.
 */
void handleStart() {
  if (!authorized()) {
    sendJson(401, "{\"error\":\"unauthorized\"}");
    return;
  }

  if (phase != IDLE) {
    sendJson(409, "{\"error\":\"session_in_progress\"}");
    Serial.println("comando /start recusado: sessao em andamento");
    return;
  }

  String body = server.arg("plain");
  long sessionId = readJsonNumber(body, "sessionId", -1);
  long durationSeconds = readJsonNumber(body, "durationSeconds", 0);
  long startDelaySeconds = readJsonNumber(body, "startDelaySeconds", 0);

  if (durationSeconds <= 0) {
    sendJson(400, "{\"error\":\"invalid_duration\"}");
    return;
  }

  durationMs = (unsigned long)durationSeconds * 1000UL;
  if (durationMs > MAX_SESSION_MS) {
    durationMs = MAX_SESSION_MS;
    Serial.println("duracao acima do teto; limitada a MAX_SESSION_MS");
  }

  startDelayMs = (unsigned long)max(0L, startDelaySeconds) * 1000UL;
  currentSessionId = sessionId;
  sessionStartMs = millis();
  phase = startDelayMs > 0 ? STARTING : RUNNING;

  // Sem espera configurada, o relé liga imediatamente.
  setRelay(phase == RUNNING);

  Serial.printf("[sessao %ld] aceita: %lus de espera, %lus de duracao\n",
                sessionId, startDelayMs / 1000, durationMs / 1000);

  sendJson(200, "{\"status\":\"started\"}");
}

/**
 * Desliga antes do fim previsto.
 *
 * Idempotente de propósito: o backend chama isto no cancelamento e na
 * expiração, e parar uma cadeira já parada não é erro.
 */
void handleStop() {
  if (!authorized()) {
    sendJson(401, "{\"error\":\"unauthorized\"}");
    return;
  }

  if (phase != IDLE) {
    endSession("comando /stop");
  }
  sendJson(200, "{\"status\":\"stopped\"}");
}

/**
 * Aciona o relé por alguns segundos, sem sessão.
 *
 * Existe para conferir a fiação na instalação: se o relé clica e a cadeira
 * liga, o problema de um agendamento que não funcionou está no software, não
 * no hardware. Só o ADMIN alcança isto pelo backend.
 *
 * Corpo opcional: {"durationSeconds":10}
 */
void handleRelayTest() {
  if (!authorized()) {
    sendJson(401, "{\"error\":\"unauthorized\"}");
    return;
  }

  // Não atropela sessão real: alguém pode estar usando a cadeira agora.
  if (phase != IDLE) {
    sendJson(409, "{\"error\":\"session_in_progress\"}");
    Serial.println("[teste] recusado: sessao em andamento");
    return;
  }

  long seconds = readJsonNumber(server.arg("plain"), "durationSeconds",
                                RELAY_TEST_SECONDS);
  if (seconds <= 0 || seconds > RELAY_TEST_MAX_SECONDS) {
    seconds = RELAY_TEST_SECONDS;
  }

  durationMs = (unsigned long)seconds * 1000UL;
  startDelayMs = 0;
  currentSessionId = -1;
  sessionStartMs = millis();
  phase = TESTING;
  setRelay(true);

  Serial.println();
  Serial.println("**********************************************************");
  Serial.printf("  TESTE DE RELE - LIGADO por %lds\n", seconds);
  Serial.printf("  Pino %d  |  nivel ativo: %s\n", RELAY_PIN,
                RELAY_ON == LOW ? "LOW" : "HIGH");
  Serial.println("  Se a cadeira nao ligou, confira VCC/GND/IN e a fonte");
  Serial.println("**********************************************************");

  sendJson(200, "{\"status\":\"relay_test_started\",\"durationSeconds\":" +
                    String(seconds) + "}");
}

/** Estado atual, para diagnóstico e para a página local. */
void handleStatus() {
  unsigned long remainingSeconds = 0;
  const char* phaseName = "idle";

  if (phase != IDLE) {
    unsigned long elapsed = millis() - sessionStartMs;

    if (elapsed < startDelayMs) {
      phaseName = "starting";
      remainingSeconds = (startDelayMs - elapsed) / 1000;
    } else {
      phaseName = phase == TESTING ? "testing" : "running";
      unsigned long usedMs = elapsed - startDelayMs;
      remainingSeconds = usedMs >= durationMs ? 0 : (durationMs - usedMs) / 1000;
    }
  }

  String json = "{";
  json += "\"phase\":\"" + String(phaseName) + "\",";
  json += "\"remainingSeconds\":" + String(remainingSeconds) + ",";
  json += "\"sessionId\":" + String(currentSessionId) + ",";
  json += "\"relayOn\":" + String(phase == RUNNING || phase == TESTING ? "true" : "false") + ",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"uptimeSeconds\":" + String(millis() / 1000);
  json += "}";

  sendJson(200, json);
}

// =========================================================================
// PÁGINA LOCAL
//
// Diagnóstico apenas: quem agenda e inicia a sessão é o aplicativo. O botão
// aqui é parada de emergência, para interromper na marra se necessário.
// =========================================================================

void handleRoot() {
  String html = R"=====(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FastRelax - Cadeira</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  :root{--gold:#c9a15c;--gold-soft:#d8b978;--gold-deep:#a67c3d;--bg:#0a0806;--line:rgba(201,161,92,.18)}
  body{font-family:'Segoe UI',-apple-system,sans-serif;min-height:100vh;display:flex;align-items:center;
       justify-content:center;padding:24px;background:radial-gradient(120% 80% at 50% -10%,#1c1712,var(--bg) 55%),var(--bg);color:#f3ece0}
  .card{width:min(92%,420px);background:linear-gradient(160deg,rgba(28,23,18,.92),rgba(12,10,8,.92));
        border:1px solid var(--line);padding:40px 32px;border-radius:28px;text-align:center;
        box-shadow:0 40px 80px rgba(0,0,0,.6)}
  .brand{display:flex;align-items:center;justify-content:center;gap:12px;margin-bottom:28px}
  .mark{width:40px;height:40px;border-radius:50%;
        background:conic-gradient(from 210deg,var(--gold-soft),var(--gold-deep),#2a2018,var(--gold-soft))}
  .name{font-size:26px;font-weight:300;letter-spacing:1px;color:var(--gold-soft)}
  h1{font-weight:300;font-size:20px;margin-bottom:6px}
  .sub{color:rgba(243,236,224,.5);font-size:13px;margin-bottom:28px}
  .dial{width:190px;height:190px;margin:0 auto 20px;border-radius:50%;display:flex;align-items:center;
        justify-content:center;border:1px solid var(--line);background:radial-gradient(circle at 50% 40%,#1a150f,#0c0a08)}
  .dial.on{border-color:rgba(216,185,120,.6);box-shadow:0 0 40px rgba(201,161,92,.4)}
  .timer{font-size:42px;font-weight:200;font-variant-numeric:tabular-nums;color:rgba(243,236,224,.55)}
  .dial.on .timer{color:var(--gold-soft)}
  .status{font-size:13px;color:rgba(243,236,224,.7);min-height:20px;margin-bottom:22px}
  .meta{font-size:11px;color:rgba(243,236,224,.35);line-height:1.7;margin-bottom:22px;font-variant-numeric:tabular-nums}
  button{width:100%;padding:16px;border-radius:30px;font-size:13px;font-weight:600;letter-spacing:2px;
         text-transform:uppercase;cursor:pointer;border:1px solid var(--line);background:transparent;color:var(--gold-soft)}
  button:disabled{opacity:.35;cursor:default}
</style></head><body>
<div class="card">
  <div class="brand"><div class="mark"></div><div class="name">Lanx</div></div>
  <h1>Cadeira FastRelax</h1>
  <div class="sub">Controlada pelo aplicativo</div>
  <div class="dial" id="dial"><div class="timer" id="timer">--</div></div>
  <div class="status" id="status">Carregando...</div>
  <div class="meta" id="meta"></div>
  <button id="btn" onclick="stopSession()" disabled>Parada de emergencia</button>
</div>
<script>
const TOKEN = ')=====";

  html += String(DEVICE_TOKEN);

  html += R"=====(';
async function stopSession(){
  if(!confirm('Interromper a sessao em andamento?'))return;
  await fetch('/stop',{method:'POST',headers:{'X-Device-Token':TOKEN}});
}
function tick(){
  fetch('/status').then(r=>r.json()).then(d=>{
    const dial=document.getElementById('dial'),timer=document.getElementById('timer');
    const status=document.getElementById('status'),btn=document.getElementById('btn');
    document.getElementById('meta').innerHTML =
      'MAC '+d.mac+'<br>IP '+d.ip+' &middot; sinal '+d.rssi+' dBm<br>ligado ha '+
      Math.floor(d.uptimeSeconds/60)+' min';

    if(d.phase==='idle'){
      dial.classList.remove('on');timer.innerHTML='--';
      status.innerHTML='Aguardando comando do aplicativo';
      btn.disabled=true;return;
    }
    btn.disabled=false;
    if(d.phase==='starting'){
      dial.classList.remove('on');timer.innerHTML=d.remainingSeconds;
      status.innerHTML='Preparando... acomode-se';
    }else{
      dial.classList.add('on');
      const m=Math.floor(d.remainingSeconds/60),s=d.remainingSeconds%60;
      timer.innerHTML=String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
      status.innerHTML='Sessao em andamento &middot; #'+d.sessionId;
    }
  }).catch(()=>{document.getElementById('status').innerHTML='Sem resposta do dispositivo';});
}
setInterval(tick,1000);tick();
</script></body></html>
)=====";

  server.send(200, "text/html", html);
}

// =========================================================================
// HEARTBEAT
// =========================================================================

/**
 * Avisa o backend que está vivo e informa o endereço atual.
 *
 * O IP vai em toda batida porque o DHCP pode trocá-lo sem aviso — é assim que
 * o backend sabe para onde mandar o próximo comando. A porta não é enviada: o
 * backend assume 80, que é onde este firmware escuta.
 */
void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  String url = "http://" + String(BACKEND_HOST) + ":" + String(BACKEND_PORT) +
               String(BACKEND_PATH) + "/chairs/heartbeat";

  http.setConnectTimeout(4000);
  http.setTimeout(4000);

  if (!http.begin(url)) {
    Serial.println("heartbeat: falha ao montar a requisicao");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  String payload = "{\"macAddress\":\"" + WiFi.macAddress() +
                   "\",\"ipAddress\":\"" + WiFi.localIP().toString() + "\"}";

  int code = http.POST(payload);

  if (code == 200) {
    if (!registeredInBackend) {
      registeredInBackend = true;
      Serial.println("heartbeat: cadeira reconhecida pelo backend");
    } else {
      Serial.println("heartbeat: ok");
    }
  } else if (code == 401) {
    Serial.println("heartbeat: token recusado — confira DEVICE_TOKEN no config.h");
  } else if (code == 404) {
    // Repete a identificação: é exatamente o dado que falta para resolver.
    registeredInBackend = false;
    printIdentity("CADEIRA AINDA NAO CADASTRADA");
  } else if (code > 0) {
    Serial.printf("heartbeat: backend respondeu %d\n", code);
  } else {
    Serial.printf("heartbeat: sem resposta (%s)\n", http.errorToString(code).c_str());
  }

  http.end();
}

// =========================================================================
// WI-FI
// =========================================================================

void connectWifi() {
  Serial.printf("conectando em %s...\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  // Sem isto o ESP32 desliga o rádio entre pacotes e demora a responder aos
  // comandos do backend.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long deadline = millis() + 20000UL;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    printIdentity("CADEIRA CONECTADA");
  } else {
    Serial.println("nao foi possivel conectar; tentando de novo em background");
    // Sem rede o IP ainda não existe, mas o MAC já vale para adiantar o cadastro.
    Serial.printf("MAC address: %s\n", WiFi.macAddress().c_str());
  }
}

// =========================================================================
// SETUP / LOOP
// =========================================================================

void setup() {
  Serial.begin(115200);
  delay(800);

  // Antes de qualquer coisa: relé desligado. Um reset no meio da sessão não
  // pode deixar a cadeira ligada sozinha.
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  connectWifi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/relay-test", HTTP_POST, handleRelayTest);

  // O WebServer descarta cabeçalhos não declarados, e é neste que vem o token.
  const char* headers[] = {"X-Device-Token"};
  server.collectHeaders(headers, 1);

  server.begin();
  Serial.println("servidor HTTP na porta 80");

  // Primeira batida logo no boot, para o painel mostrar a cadeira sem esperar
  // o intervalo cheio.
  sendHeartbeat();
  lastHeartbeatMs = millis();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  // ---- Reconexão do Wi-Fi ----
  // Não bloqueia: uma sessão em andamento continua contando e o relé desliga
  // na hora certa mesmo sem rede.
  static bool wasConnected = true;

  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected) {
      wasConnected = false;
      Serial.println("wi-fi caiu; reconectando em background");
    }
    if (now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
      lastWifiAttemptMs = now;
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  } else if (!wasConnected) {
    wasConnected = true;
    // O DHCP pode ter entregado outro endereço; o heartbeat seguinte avisa o
    // backend, e o serial mostra para quem estiver acompanhando.
    printIdentity("CADEIRA RECONECTADA");
    sendHeartbeat();
    lastHeartbeatMs = now;
  }

  // ---- Heartbeat ----
  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    sendHeartbeat();
  }

  // ---- Máquina de estados da sessão ----
  if (phase == STARTING && now - sessionStartMs >= startDelayMs) {
    phase = RUNNING;
    setRelay(true);
    Serial.printf("[sessao %ld] rele ligado\n", currentSessionId);
  }

  if (phase == RUNNING && now - sessionStartMs >= startDelayMs + durationMs) {
    endSession("tempo concluido");
  }

  // O teste manual usa o mesmo relógio, mas encerra com log próprio: quem está
  // com o monitor aberto precisa ver o desligamento sem confundir com sessão.
  if (phase == TESTING && now - sessionStartMs >= durationMs) {
    setRelay(false);
    phase = IDLE;
    Serial.println();
    Serial.println("**********************************************************");
    Serial.println("  TESTE DE RELE - DESLIGADO");
    Serial.println("**********************************************************");
    Serial.println();
  }
}
