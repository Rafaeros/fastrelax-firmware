#include "backend/backend_client.h"

#include <Arduino.h>
#include <HTTPClient.h>

#include "util/identity.h"
#include "config.h"
#include "network/network_config.h"
#include "network/network_manager.h"
#include "session/session_manager.h"

namespace backend {
namespace {

unsigned long lastHeartbeatMs = 0;

// Enquanto o backend não reconhecer o MAC, o banner de cadastro é reimpresso a
// cada heartbeat — quem está com o monitor serial aberto não precisa procurar
// a informação no meio do log.
bool registeredInBackend = false;

String heartbeatUrl() {
  return "http://" + String(BACKEND_HOST) + ":" + String(BACKEND_PORT) +
         String(BACKEND_PATH) + "/chairs/heartbeat";
}

}  // namespace

void begin() {
  sendHeartbeat();
  lastHeartbeatMs = millis();
}

void loop() {
  unsigned long now = millis();
  if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
    return;
  }
  lastHeartbeatMs = now;
  sendHeartbeat();
}

void sendHeartbeat() {
  if (!network::isConnected()) {
    return;
  }

  HTTPClient http;
  String url = heartbeatUrl();

  http.setConnectTimeout(4000);
  http.setTimeout(4000);

  if (!http.begin(url)) {
    Serial.println("heartbeat: falha ao montar a requisicao");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  // A fase vai junto para o backend saber o estado sem perguntar. É o que
  // separa "a cadeira recusou porque está estabilizando" de "a cadeira não
  // respondeu": no primeiro caso o backend já sabia, e sabe por quanto tempo.
  session::Snapshot state = session::snapshot();

  // A rede em uso vai junto: é assim que o backend sabe se a configuração que
  // ele enviou foi de fato aplicada, e não só aceita. Sem isso, uma cadeira que
  // recebeu o SSID novo e não conseguiu entrar nele apareceria como configurada.
  network_config::Credentials net = network_config::current();

  String payload = "{\"macAddress\":\"" + network::macAddress() +
                   "\",\"ipAddress\":\"" + network::ipAddress() +
                   "\",\"phase\":\"" + String(state.phaseName) +
                   "\",\"ssid\":\"" + net.ssid +
                   "\",\"bssid\":\"" + net.bssid +
                   "\",\"provisioned\":" +
                   String(network_config::isProvisioned() ? "true" : "false") +
                   ",\"remainingSeconds\":" + String(state.remainingSeconds) + "}";

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
    identity::print("CADEIRA AINDA NAO CADASTRADA");
  } else if (code > 0) {
    Serial.printf("heartbeat: backend respondeu %d\n", code);
  } else {
    Serial.printf("heartbeat: sem resposta (%s)\n", http.errorToString(code).c_str());
  }

  http.end();
}

void forgetRegistration() {
  registeredInBackend = false;
}

}  // namespace backend
