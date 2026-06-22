#include <WiFi.h>
#include <WebServer.h>

// Configurações da Rede WiFi (Access Point)
const char* ssid = "Projeto Anti-Stress";

WebServer server(80);

// Configuração do Hardware
#define RELAY_PIN 26

// Variáveis de Controle
bool timerRunning = false;
unsigned long startTime = 0;

const unsigned long PRE_START_MS = 10000;  // 10 segundos de preparação
const unsigned long MASSAGE_MS   = 300000; // 5 minutos de sessão

// Interface de Usuário (HTML/CSS/JS)
String page() {
  return R"=====(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Projeto Anti-Stress</title>
<style>
  /* Reset e Tipografia Minimalista */
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, sans-serif;
    height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    /* Paleta de Cores: Confiança e Paz (Azul Oceânico Profundo) */
    background: linear-gradient(-45deg, #0f2027, #203a43, #2c5364);
    background-size: 400% 400%;
    animation: gradientBG 15s ease infinite;
    color: #ffffff;
    overflow: hidden;
  }
  
  @keyframes gradientBG {
    0% { background-position: 0% 50%; }
    50% { background-position: 100% 50%; }
    100% { background-position: 0% 50%; }
  }

  /* Efeito Glassmorphism Premium */
  .card {
    width: min(90%, 450px);
    background: rgba(255, 255, 255, 0.05);
    backdrop-filter: blur(24px);
    -webkit-backdrop-filter: blur(24px);
    border: 1px solid rgba(255, 255, 255, 0.1);
    padding: 40px 30px;
    border-radius: 30px;
    box-shadow: 0 30px 60px rgba(0,0,0, 0.4), inset 0 1px 0 rgba(255,255,255,0.1);
    text-align: center;
    transform: translateY(20px);
    opacity: 0;
    animation: floatUp 1s cubic-bezier(0.23, 1, 0.32, 1) forwards;
  }

  @keyframes floatUp {
    to { transform: translateY(0); opacity: 1; }
  }

  h1 { font-weight: 300; font-size: 28px; margin-bottom: 8px; letter-spacing: 1px; }
  .subtitle { opacity: 0.6; font-size: 14px; margin-bottom: 35px; font-weight: 300; }

  /* Timer Dinâmico */
  .timer {
    font-size: 80px;
    font-weight: 200;
    margin: 10px 0 20px 0;
    text-shadow: 0 0 20px rgba(255,255,255,0.2);
    font-variant-numeric: tabular-nums;
    transition: all 0.5s ease;
  }

  .breathe {
    animation: breathe 4s ease-in-out infinite;
  }

  @keyframes breathe {
    0%, 100% { opacity: 0.8; transform: scale(0.98); }
    50% { opacity: 1; transform: scale(1.02); text-shadow: 0 0 30px rgba(255,255,255,0.5); }
  }

  /* Barra de Progresso Suave */
  .progress-bg {
    width: 100%;
    height: 8px;
    background: rgba(255, 255, 255, 0.1);
    border-radius: 20px;
    overflow: hidden;
    margin: 25px 0;
  }

  .progress-bar {
    height: 100%;
    width: 0%;
    /* Gradiente Ciano para sensação de fluxo/energia limpa */
    background: linear-gradient(90deg, #4facfe 0%, #00f2fe 100%);
    border-radius: 20px;
    transition: width 1s linear;
    box-shadow: 0 0 10px rgba(0, 242, 254, 0.4);
  }

  .status {
    font-size: 15px;
    font-weight: 300;
    margin-bottom: 35px;
    min-height: 22px;
    letter-spacing: 0.5px;
    opacity: 0.9;
  }

  /* Botão Tátil e Responsivo */
  button {
    background: rgba(255, 255, 255, 0.1);
    color: white;
    border: 1px solid rgba(255, 255, 255, 0.2);
    padding: 18px 32px;
    border-radius: 30px;
    font-size: 15px;
    font-weight: 600;
    letter-spacing: 1.5px;
    cursor: pointer;
    transition: all 0.3s cubic-bezier(0.25, 0.8, 0.25, 1);
    text-transform: uppercase;
    width: 100%;
    outline: none;
  }

  button:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.2);
    box-shadow: 0 0 20px rgba(255, 255, 255, 0.15);
    transform: translateY(-2px);
  }

  button:active:not(:disabled) {
    transform: translateY(1px);
  }

  button:disabled {
    opacity: 0.4;
    cursor: default;
  }

  .btn-glow {
    animation: pulseBtn 2s infinite;
  }

  @keyframes pulseBtn {
    0% { box-shadow: 0 0 0 0 rgba(255,255,255,0.2); }
    70% { box-shadow: 0 0 0 15px rgba(255,255,255,0); }
    100% { box-shadow: 0 0 0 0 rgba(255,255,255,0); }
  }
</style>
</head>
<body>

<div class="card">
  <h1>Projeto Anti-Stress</h1>
  <div class="subtitle">Sua sessão de relaxamento está pronta</div>

  <div class="timer breathe" id="timer">Pronto</div>

  <div class="progress-bg">
    <div class="progress-bar" id="bar"></div>
  </div>

  <div class="status" id="status">Aguardando início</div>

  <button id="btn" onclick="startSession()">INICIAR SESSÃO</button>
</div>

<script>
  async function startSession() {
    const btn = document.getElementById("btn");
    btn.disabled = true;
    btn.classList.add("btn-glow");
    btn.innerText = "CONECTANDO...";
    
    try {
      await fetch('/start');
    } catch(e) {
      console.error(e);
    }
  }

  function update() {
    fetch('/status')
      .then(r => r.json())
      .then(data => {
        const timer = document.getElementById('timer');
        const status = document.getElementById('status');
        const bar = document.getElementById('bar');
        const btn = document.getElementById('btn');

        if (!data.running) {
          status.innerHTML = "Sessão concluída. Respire fundo.";
          bar.style.width = "100%";
          timer.innerHTML = "Pronto";
          timer.classList.add("breathe");
          
          btn.disabled = false;
          btn.classList.remove("btn-glow");
          btn.innerText = "INICIAR NOVA SESSÃO";
          return;
        }

        btn.innerText = "SESSÃO EM ANDAMENTO";
        timer.classList.remove("breathe");

        if (data.phase === "starting") {
          timer.innerHTML = data.remaining;
          status.innerHTML = "Iniciando em " + data.remaining + " segundos";
          let p = ((10 - data.remaining) / 10) * 100;
          bar.style.width = p + "%";
        }

        if (data.phase === "massage") {
          let m = Math.floor(data.remaining / 60);
          let s = data.remaining % 60;

          timer.innerHTML = String(m).padStart(2, '0') + ":" + String(s).padStart(2, '0');
          status.innerHTML = "Relaxe. Sessão em andamento.";

          let p = ((300 - data.remaining) / 300) * 100;
          bar.style.width = p + "%";
        }
      })
      .catch(err => console.log("Aguardando ESP32..."));
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
    digitalWrite(RELAY_PIN, HIGH);
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{";
  json += "\"running\":";
  json += timerRunning ? "true" : "false";
  json += ",";

  if (!timerRunning) {
    json += "\"phase\":\"idle\",\"remaining\":0";
  } else {
    unsigned long elapsed = millis() - startTime;

    if (elapsed < PRE_START_MS) {
      json += "\"phase\":\"starting\",\"remaining\":" + String((PRE_START_MS - elapsed) / 1000);
    } else {
      long rem = (MASSAGE_MS - (elapsed - PRE_START_MS)) / 1000;
      if (rem < 0) rem = 0;
      json += "\"phase\":\"massage\",\"remaining\":" + String(rem);
    }
  }
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  // Configuração do Pino do Relé
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

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
      digitalWrite(RELAY_PIN, LOW);
      timerRunning = false;
      Serial.println("Sessão finalizada. Relé desligado.");
    }
  }
}