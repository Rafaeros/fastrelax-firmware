#include "api/device_server.h"

#include <Arduino.h>
#include <WebServer.h>

#include "api/portal_page.h"
#include "config.h"
#include "network/network_config.h"
#include "network/network_manager.h"
#include "session/session_manager.h"
#include "util/json_reader.h"

namespace device_api {
namespace {

WebServer server(80);

void sendJson(int code, const String& json) {
  server.send(code, "application/json", json);
}

/** Todo comando do backend precisa trazer o token combinado. */
bool authorized() {
  if (!server.hasHeader("X-Device-Token")) {
    return false;
  }
  return server.header("X-Device-Token") == String(DEVICE_TOKEN);
}

/** Responde 401 e devolve false quando o token não confere. */
bool rejectUnauthorized() {
  if (authorized()) {
    return false;
  }
  sendJson(401, "{\"error\":\"unauthorized\"}");
  return true;
}

/**
 * Recusa por estabilização, com os segundos que faltam.
 *
 * O backend precisa do número para decidir entre remarcar e avisar quem está
 * na frente da cadeira — um 409 seco não distingue "ocupada" de "espere mais
 * meio minuto".
 */
void sendCoolingDown() {
  sendJson(409, "{\"error\":\"cooling_down\",\"remainingSeconds\":" +
                    String(session::snapshot().remainingSeconds) + "}");
}

/**
 * Corpo: {"sessionId":42,"durationSeconds":300,"startDelaySeconds":5}
 */
void handleStart() {
  if (rejectUnauthorized()) {
    return;
  }

  String body = server.arg("plain");
  long sessionId = json::readNumber(body, "sessionId", -1);
  long durationSeconds = json::readNumber(body, "durationSeconds", 0);
  long startDelaySeconds = json::readNumber(body, "startDelaySeconds", 0);

  switch (session::start(sessionId, durationSeconds, startDelaySeconds)) {
    case session::StartResult::BUSY:
      sendJson(409, "{\"error\":\"session_in_progress\"}");
      return;
    case session::StartResult::COOLING_DOWN:
      sendCoolingDown();
      return;
    case session::StartResult::INVALID_DURATION:
      sendJson(400, "{\"error\":\"invalid_duration\"}");
      return;
    case session::StartResult::ACCEPTED:
      sendJson(200, "{\"status\":\"started\"}");
      return;
  }
}

/**
 * Idempotente de propósito: o backend chama isto no cancelamento e na
 * expiração, e parar uma cadeira já parada não é erro.
 */
void handleStop() {
  if (rejectUnauthorized()) {
    return;
  }

  session::stop("comando /stop");
  sendJson(200, "{\"status\":\"stopped\"}");
}

/**
 * Existe para conferir a fiação na instalação: se o relé clica e a cadeira
 * liga, o problema de um agendamento que não funcionou está no software, não
 * no hardware. Só o ADMIN alcança isto pelo backend.
 *
 * Corpo opcional: {"durationSeconds":10}
 */
void handleRelayTest() {
  if (rejectUnauthorized()) {
    return;
  }

  long seconds = json::readNumber(server.arg("plain"), "durationSeconds",
                                  RELAY_TEST_SECONDS);

  switch (session::startRelayTest(seconds)) {
    case session::StartResult::BUSY:
      sendJson(409, "{\"error\":\"session_in_progress\"}");
      return;
    case session::StartResult::COOLING_DOWN:
      sendCoolingDown();
      return;
    default:
      break;
  }

  // A sessão pode ter limitado a duração pedida; devolve o que de fato valeu.
  sendJson(200, "{\"status\":\"relay_test_started\",\"durationSeconds\":" +
                    String(session::plannedDurationSeconds()) + "}");
}

void handleStatus() {
  session::Snapshot state = session::snapshot();

  String json = "{";
  json += "\"phase\":\"" + String(state.phaseName) + "\",";
  json += "\"remainingSeconds\":" + String(state.remainingSeconds) + ",";
  json += "\"sessionId\":" + String(state.sessionId) + ",";
  json += "\"chairOn\":" + String(state.chairOn ? "true" : "false") + ",";
  json += "\"mac\":\"" + network::macAddress() + "\",";
  json += "\"ip\":\"" + network::ipAddress() + "\",";
  json += "\"rssi\":" + String(network::signalStrength()) + ",";
  // Diz se a cadeira ainda roda a rede de fábrica: é o que o backend usa para
  // marcar a configuração como aplicada de verdade, e não só como enviada.
  json += "\"ssid\":\"" + network_config::current().ssid + "\",";
  json += "\"bssid\":\"" + network_config::current().bssid + "\",";
  json += "\"provisioned\":" + String(network_config::isProvisioned() ? "true" : "false") + ",";
  json += "\"uptimeSeconds\":" + String(millis() / 1000);
  json += "}";

  sendJson(200, json);
}

/**
 * Corpo: {"ssid":"REDE","password":"senha","bssid":"AA:BB:CC:DD:EE:FF"}
 *
 * <p>
 * Grava a rede da empresa na NVS e reconecta. É o que substitui recompilar o
 * firmware com o SSID do cliente e ir até a cadeira regravar a placa.
 *
 * <p>
 * A resposta sai <em>antes</em> de reconectar. O backend está falando com a
 * cadeira pela rede que a troca vai derrubar: reconectar primeiro deixaria a
 * requisição sem resposta, e o backend não saberia se a configuração chegou.
 */
void handleNetwork() {
  if (rejectUnauthorized()) {
    return;
  }

  String body = server.arg("plain");
  String ssid = json::readString(body, "ssid");
  String password = json::readString(body, "password");
  String bssid = json::readString(body, "bssid");

  if (ssid.isEmpty()) {
    sendJson(400, "{\"error\":\"ssid_required\"}");
    return;
  }

  // BSSID inválido é recusado aqui, e não ignorado em silêncio: quem digitou o
  // MAC errado precisa saber, senão a cadeira fica num AP qualquer e ninguém
  // entende por que a fixação "não funcionou".
  uint8_t parsed[6];
  if (!bssid.isEmpty() && !network_config::parseBssid(bssid, parsed)) {
    sendJson(400, "{\"error\":\"invalid_bssid\"}");
    return;
  }

  bool changed = network_config::save(ssid, password, bssid);

  sendJson(200, String("{\"saved\":true,\"changed\":") +
                    (changed ? "true" : "false") + ",\"ssid\":\"" + ssid + "\"}");

  if (changed) {
    // Só depois de a resposta sair. Reaplicar a mesma rede derrubaria o Wi-Fi
    // por alguns segundos à toa — e o backend reenvia a configuração sempre que
    // a empresa é salva, mesmo sem nada ter mudado.
    network::applyConfig();
  }
}

void handleRoot() {
  server.send(200, "text/html", portal::pageHtml());
}

}  // namespace

void begin() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/relay-test", HTTP_POST, handleRelayTest);
  server.on("/network", HTTP_POST, handleNetwork);

  // O WebServer descarta cabeçalhos não declarados, e é neste que vem o token.
  const char* headers[] = {"X-Device-Token"};
  server.collectHeaders(headers, 1);

  server.begin();
  Serial.println("servidor HTTP na porta 80");
}

void loop() {
  server.handleClient();
}

}  // namespace device_api
