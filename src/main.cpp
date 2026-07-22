#include <WiFi.h>
#include <WebServer.h>

// Configurações da Rede WiFi (Access Point)
const char* ssid = "Projeto Anti-Stress";

WebServer server(80);

// Configuração do Hardware
#define RELAY_PIN 26

// Nível lógico do módulo relé.
// Se o relé acionar invertido (liga quando devia desligar), troque estes dois valores.
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// Variáveis de Controle
bool timerRunning = false;
unsigned long startTime = 0;

const unsigned long PRE_START_MS = 10000;  // 10 segundos de preparação
const unsigned long MASSAGE_MS   = 300000; // 5 minutos de sessão

void setRelay(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}

// Interface de Usuário (HTML/CSS/JS)
String page() {
  return R"=====(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lanx</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }

  :root {
    --gold: #c9a15c;
    --gold-soft: #d8b978;
    --gold-deep: #a67c3d;
    --bg-0: #0a0806;
    --line: rgba(201, 161, 92, 0.18);
  }

  body {
    font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    background: radial-gradient(120% 80% at 50% -10%, #1c1712 0%, var(--bg-0) 55%), var(--bg-0);
    color: #f3ece0;
    overflow: hidden;
  }

  body::before {
    content: "";
    position: fixed;
    inset: 0;
    background: radial-gradient(60% 50% at 50% 50%, rgba(201,161,92,0.10), transparent 70%);
    pointer-events: none;
  }

  .card {
    position: relative;
    z-index: 1;
    width: min(92%, 420px);
    background: linear-gradient(160deg, rgba(28,23,18,0.92), rgba(12,10,8,0.92));
    backdrop-filter: blur(18px);
    -webkit-backdrop-filter: blur(18px);
    border: 1px solid var(--line);
    padding: 46px 34px 40px;
    border-radius: 28px;
    box-shadow: 0 40px 80px rgba(0,0,0,0.6), inset 0 1px 0 rgba(216,185,120,0.12);
    text-align: center;
    transform: translateY(18px);
    opacity: 0;
    animation: floatUp 0.9s cubic-bezier(0.23,1,0.32,1) forwards;
  }
  @keyframes floatUp { to { transform: translateY(0); opacity: 1; } }

  .brand {
    display: flex; align-items: center; justify-content: center;
    gap: 12px; margin-bottom: 34px;
  }
  .brand .mark {
    width: 40px; height: 40px; border-radius: 50%;
    background: conic-gradient(from 210deg, var(--gold-soft), var(--gold-deep), #2a2018, var(--gold-soft));
    box-shadow: 0 0 18px rgba(201,161,92,0.35), inset 0 0 8px rgba(0,0,0,0.5);
  }
  .brand .name {
    font-size: 26px; font-weight: 300; letter-spacing: 1px; color: var(--gold-soft);
  }

  h1 { font-weight: 300; font-size: 22px; letter-spacing: 0.5px; }
  .subtitle {
    color: rgba(243,236,224,0.5); font-size: 13px;
    margin: 8px 0 34px; font-weight: 300; letter-spacing: 0.3px;
  }

  /* Anel + timer */
  .dial {
    width: 210px; height: 210px; margin: 0 auto 14px;
    border-radius: 50%; position: relative;
    display: flex; align-items: center; justify-content: center;
    border: 1px solid var(--line);
    background: radial-gradient(circle at 50% 40%, #1a150f, #0c0a08);
    transition: all 0.5s ease;
  }
  .dial::after {
    content: ""; position: absolute; inset: -1px; border-radius: 50%;
    box-shadow: 0 0 0 0 rgba(201,161,92,0); transition: box-shadow 0.5s ease;
  }
  .dial.active { border-color: rgba(216,185,120,0.6); }
  .dial.active::after { animation: pulse 2.4s ease-in-out infinite; }
  @keyframes pulse {
    0%,100% { box-shadow: 0 0 30px 2px rgba(201,161,92,0.30); }
    50% { box-shadow: 0 0 55px 8px rgba(201,161,92,0.55); }
  }

  .timer {
    font-size: 46px; font-weight: 200; letter-spacing: 1px;
    font-variant-numeric: tabular-nums;
    color: rgba(243,236,224,0.55); transition: color 0.4s ease;
  }
  .dial.active .timer { color: var(--gold-soft); text-shadow: 0 0 18px rgba(201,161,92,0.5); }

  /* Barra de progresso */
  .progress-bg {
    width: 100%; height: 6px; margin: 22px 0 10px;
    background: rgba(255,255,255,0.06); border-radius: 20px; overflow: hidden;
  }
  .progress-bar {
    height: 100%; width: 0%; border-radius: 20px;
    background: linear-gradient(90deg, var(--gold-deep), var(--gold-soft));
    box-shadow: 0 0 10px rgba(201,161,92,0.4);
    transition: width 1s linear;
  }

  .status {
    font-size: 13px; font-weight: 300; letter-spacing: 0.4px;
    min-height: 20px; margin-bottom: 30px; color: rgba(243,236,224,0.7);
  }

  button {
    width: 100%; padding: 18px 28px; border-radius: 30px;
    font-size: 14px; font-weight: 600; letter-spacing: 2px;
    text-transform: uppercase; cursor: pointer; outline: none;
    color: #14100b; border: none;
    background: linear-gradient(135deg, var(--gold-soft), var(--gold-deep));
    box-shadow: 0 10px 24px rgba(166,124,61,0.35);
    transition: all 0.3s cubic-bezier(0.25,0.8,0.25,1);
  }
  button:hover:not(:disabled) {
    transform: translateY(-2px); box-shadow: 0 14px 30px rgba(201,161,92,0.5);
  }
  button:active:not(:disabled) { transform: translateY(1px); }
  button:disabled { opacity: 0.45; cursor: default; box-shadow: none; }
</style>
</head>
<body>

<div class="card">
  <div class="brand">
    <div class="mark"></div>
    <div class="name">Lanx</div>
  </div>

  <h1>Controle da Poltrona</h1>
  <div class="subtitle">Sua sessão de relaxamento está pronta</div>

  <div class="dial" id="dial">
    <div class="timer" id="timer">Pronto</div>
  </div>

  <div class="progress-bg"><div class="progress-bar" id="bar"></div></div>
  <div class="status" id="status">Aguardando início</div>

  <button id="btn" onclick="startSession()">INICIAR SESSÃO</button>
</div>

<script>
  async function startSession() {
    const btn = document.getElementById("btn");
    btn.disabled = true;
    btn.innerText = "CONECTANDO...";
    try { await fetch('/start'); } catch(e) { console.error(e); }
  }

  function update() {
    fetch('/status').then(r => r.json()).then(data => {
      const dial = document.getElementById('dial');
      const timer = document.getElementById('timer');
      const status = document.getElementById('status');
      const bar = document.getElementById('bar');
      const btn = document.getElementById('btn');

      if (!data.running) {
        dial.classList.remove('active');
        timer.innerHTML = "Pronto";
        status.innerHTML = "Sessão concluída. Respire fundo.";
        bar.style.width = "0%";
        btn.disabled = false;
        btn.innerText = "INICIAR SESSÃO";
        return;
      }

      dial.classList.add('active');
      btn.disabled = true;
      btn.innerText = "SESSÃO EM ANDAMENTO";

      if (data.phase === "starting") {
        timer.innerHTML = data.remaining;
        status.innerHTML = "Iniciando em " + data.remaining + "s";
        bar.style.width = (((10 - data.remaining) / 10) * 100) + "%";
      }
      if (data.phase === "massage") {
        let m = Math.floor(data.remaining / 60);
        let s = data.remaining % 60;
        timer.innerHTML = String(m).padStart(2,'0') + ":" + String(s).padStart(2,'0');
        status.innerHTML = "Relaxe. Sessão em andamento.";
        bar.style.width = (((300 - data.remaining) / 300) * 100) + "%";
      }
    }).catch(err => console.log("Aguardando ESP32..."));
  }

  setInterval(update, 1000);
  update();
</script>
</body>
</html>
)=====";
}

// Rotas do WebServer
void handleRoot() {
  server.send(200, "text/html", page());
}

void handleStart() {
  if (!timerRunning) {
    timerRunning = true;
    startTime = millis();
    setRelay(true);
    Serial.println("Sessão iniciada. Relé ligado.");
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{\"running\":";
  json += timerRunning ? "true" : "false";
  json += ",";

  if (!timerRunning) {
    json += "\"phase\":\"idle\",\"remaining\":0";
  } else {
    unsigned long elapsed = millis() - startTime;
    if (elapsed < PRE_START_MS) {
      json += "\"phase\":\"starting\",\"remaining\":" + String((PRE_START_MS - elapsed) / 1000);
    } else {
      long rem = (long)(MASSAGE_MS - (elapsed - PRE_START_MS)) / 1000;
      if (rem < 0) rem = 0;
      json += "\"phase\":\"massage\",\"remaining\":" + String(rem);
    }
  }
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(1000); // aguarda USB enumerar no C3

  // Configuração do Pino do Relé
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false); // inicia desligado

  // Inicia o Access Point
  WiFi.softAP(ssid);
  Serial.print("Access Point Iniciado: ");
  Serial.println(ssid);

  // Configuração de Rotas HTTP
  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/status", handleStatus);

  server.begin();
  Serial.println("Servidor HTTP rodando.");
}

void loop() {
  server.handleClient();

  // Controle de Estado Não-Bloqueante
  if (timerRunning) {
    if (millis() - startTime >= PRE_START_MS + MASSAGE_MS) {
      setRelay(false);
      timerRunning = false;
      Serial.println("Sessão finalizada. Relé desligado.");
    }
  }
}
