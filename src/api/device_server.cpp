#include "api/device_server.h"

#include <Arduino.h>
#include <WebServer.h>

#include "api/portal_page.h"
#include "config.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_config.h"
#include "network/network_config.h"
#include "network/network_manager.h"
#include "power/power_relay.h"
#include "session/session_manager.h"
#include "state/chair_state.h"
#include "util/device_identity.h"
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
  return server.header("X-Device-Token") == device_identity::token();
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
  // Qual meio está carregando o tráfego agora. Com o failover ligado isto muda
  // sozinho, e é a única forma de saber se a cadeira caiu para o Wi-Fi.
  json += "\"link\":\"" + String(network::linkName()) + "\",";
  // Diz se a cadeira ainda roda a rede de fábrica: é o que o backend usa para
  // marcar a configuração como aplicada de verdade, e não só como enviada.
  json += "\"ssid\":\"" + network_config::current().ssid + "\",";
  json += "\"bssid\":\"" + network_config::current().bssid + "\",";
  json += "\"provisioned\":" + String(network_config::isProvisioned() ? "true" : "false") + ",";
  json += "\"active\":" + String(chair_state::isActive() ? "true" : "false") + ",";
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

/**
 * Apaga a configuração de rede gravada e volta a valer o config.h.
 *
 * <p>
 * Existe porque a NVS sobrevive à regravação do firmware: uma cadeira
 * provisionada para a rede errada não volta ao padrão de fábrica nem
 * recompilando o config.h, e sem isto a única saída seria apagar a partição NVS
 * pelo esptool, com a cadeira na bancada.
 *
 * <p>
 * A reconexão não é disparada aqui: a resposta precisa sair pela rede atual
 * antes de a cadeira trocar de AP. O enlace é reaplicado logo em seguida.
 */
void handleNetworkReset() {
  if (rejectUnauthorized()) {
    return;
  }

  network_config::clear();

  sendJson(200, String("{\"cleared\":true,\"ssid\":\"") + WIFI_SSID + "\"}");

  network::applyConfig();
}

/**
 * Corpo: {"host":"10.48.0.189","port":1883,"username":"fastrelax","password":"..."}
 *
 * <p>
 * Grava o broker MQTT desta cadeira na NVS e força reconexão. É o que
 * substitui recompilar o firmware com a credencial de config.h só pra trocar
 * o broker de um device específico — por padrão nenhuma cadeira chama isto e
 * todas usam o mesmo broker de config.h; só passa a ser por device quando o
 * backend grava algo aqui.
 *
 * <p>
 * `port` é opcional: omitido ou 0 mantém a porta já gravada (ou a de
 * config.h, se nunca provisionada).
 *
 * <p>
 * A resposta nunca inclui a senha, nem a recebida nem a antiga — mesmo
 * critério do MAC/token: nada que dê acesso ao broker deve passar por uma
 * rota que ecoa em log de proxy ou console do navegador.
 */
void handleMqtt() {
  if (rejectUnauthorized()) {
    return;
  }

  String body = server.arg("plain");
  String host = json::readString(body, "host");
  long port = json::readNumber(body, "port", 0);
  String username = json::readString(body, "username");
  String password = json::readString(body, "password");

  if (host.isEmpty()) {
    sendJson(400, "{\"error\":\"host_required\"}");
    return;
  }

  if (port < 0 || port > 65535) {
    sendJson(400, "{\"error\":\"invalid_port\"}");
    return;
  }

  bool changed = mqtt_config::save(host, static_cast<uint16_t>(port), username, password);

  sendJson(200, String("{\"saved\":true,\"changed\":") + (changed ? "true" : "false") +
                    ",\"host\":\"" + host + "\"}");

  if (changed) {
    // Mesmo motivo do /network: a resposta sai antes de derrubar o canal que
    // a troca de broker pode afetar (se o comando tivesse vindo por MQTT — por
    // HTTP não tem esse risco, mas o padrão fica igual em toda a API).
    mqtt::applyConfig();
  }
}

/**
 * Apaga a configuração de MQTT gravada e volta a valer o broker de config.h.
 *
 * <p>
 * Mesmo motivo de /network/reset: a NVS sobrevive à regravação do firmware,
 * então uma cadeira provisionada pro broker errado não volta ao padrão só
 * recompilando.
 */
void handleMqttReset() {
  if (rejectUnauthorized()) {
    return;
  }

  mqtt_config::clear();

  sendJson(200, String("{\"cleared\":true,\"host\":\"") + MQTT_HOST + "\"}");

  mqtt::applyConfig();
}

/**
 * Corpo: {"active":true}
 *
 * <p>
 * Empurrado pelo backend logo depois do RH/Physical ativar ou desativar a
 * cadeira — sem esperar o próximo heartbeat. Se a cadeira estiver offline
 * agora, o heartbeat seguinte reconcilia sozinho (ver handleStatus e
 * backend_client.cpp), então não há retry aqui: o backend também não espera,
 * o toggle no banco já valeu.
 */
void handlePower() {
  if (rejectUnauthorized()) {
    return;
  }

  bool active = json::readBool(server.arg("plain"), "active", true);

  chair_state::setActive(active);
  power_relay::apply(active);

  sendJson(200, String("{\"applied\":true,\"active\":") +
                    (active ? "true" : "false") + "}");
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
  server.on("/network/reset", HTTP_POST, handleNetworkReset);
  server.on("/mqtt", HTTP_POST, handleMqtt);
  server.on("/mqtt/reset", HTTP_POST, handleMqttReset);
  server.on("/power", HTTP_POST, handlePower);

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
