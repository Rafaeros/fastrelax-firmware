// =========================================================================
// MQTT — implementação
//
// O mesmo código atende os dois brokers cogitados; muda só o config.h.
//
//   HiveMQ Cloud
//     TLS obrigatório na 8883, usuário e senha criados no painel. Exige
//     MQTT_USE_TLS = 1. O plano gratuito aceita QoS 0 e 1.
//
//   RabbitMQ com plugin rabbitmq_mqtt
//     `rabbitmq-plugins enable rabbitmq_mqtt` abre a 1883 (8883 com TLS). As
//     credenciais são as mesmas do AMQP, e cada tópico MQTT vira routing key
//     no exchange `amq.topic` — o backend pode consumir por AMQP normal, sem
//     falar MQTT. Duas limitações do plugin: não existe QoS 2, e a `/` do
//     tópico vira `.` na routing key (fastrelax/chairs/x -> fastrelax.chairs.x).
//
// Em ambos, a publicação sai em QoS 0: o PubSubClient não implementa outro
// nível na publicação. Para o estado da cadeira isso basta — a mensagem é
// retida e reenviada a cada MQTT_STATUS_INTERVAL_MS, então uma perda se
// corrige sozinha na batida seguinte. Os comandos são assinados em MQTT_QOS,
// onde 1 vale a pena: /start perdido é sessão que não acontece.
// =========================================================================

#include "mqtt/mqtt_client.h"

#include "config.h"

#if MQTT_ENABLED

#include <PubSubClient.h>
#include <WiFiClient.h>
#if MQTT_USE_TLS
#include <WiFiClientSecure.h>
#endif

#include "mqtt/mqtt_config.h"
#include "network/network_config.h"
#include "network/network_manager.h"
#include "power/power_relay.h"
#include "session/session_manager.h"
#include "state/chair_state.h"
#include "util/device_identity.h"
#include "util/json_reader.h"

namespace mqtt {
namespace {

#if MQTT_USE_TLS
WiFiClientSecure transport;
#else
WiFiClient transport;
#endif

PubSubClient client(transport);

unsigned long lastAttemptMs = 0;
unsigned long lastStatusMs = 0;

// PubSubClient::setServer(const char*, uint16_t) guarda o ponteiro, não uma
// cópia — precisa de um buffer que sobreviva além do escopo de connect().
// Uma String local ali seria destruída ao sair da função, deixando o
// ponteiro pendurado até a tentativa seguinte chamar setServer() de novo.
String brokerHost;

/**
 * Identificador da cadeira nos tópicos: MAC em minúsculas, sem os dois-pontos.
 *
 * `:` é legal em tópico MQTT, mas atrapalha na routing key do RabbitMQ e na
 * hora de digitar um filtro à mão no cliente de diagnóstico.
 */
String deviceId() {
  String mac = network::macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return mac;
}

String baseTopic() {
  return String(MQTT_TOPIC_PREFIX) + "/" + deviceId();
}

/**
 * Mesma checagem do cabeçalho X-Device-Token na API HTTP.
 *
 * As credenciais do broker já barram quem não é da casa, mas elas costumam ser
 * compartilhadas entre dispositivos: sem isto, qualquer cliente autenticado
 * ligaria qualquer cadeira. O token continua sendo o que autoriza o comando.
 */
bool authorized(const String& payload) {
  int tokenAt = payload.indexOf("\"token\"");
  if (tokenAt < 0) {
    return false;
  }
  int colonAt = payload.indexOf(':', tokenAt);
  if (colonAt < 0) {
    return false;
  }
  int firstQuote = payload.indexOf('"', colonAt + 1);
  if (firstQuote < 0) {
    return false;
  }
  int lastQuote = payload.indexOf('"', firstQuote + 1);
  if (lastQuote < 0) {
    return false;
  }
  return payload.substring(firstQuote + 1, lastQuote) == device_identity::token();
}

/** Devolve o desfecho do comando, para o backend não ficar cego. */
void replyToCommand(const String& status) {
  publish("ack", "{\"status\":\"" + status + "\"}", false);
}

/**
 * Recusa por estabilização, com os segundos que faltam — o backend precisa do
 * número para distinguir "ocupada" de "espere mais meio minuto".
 */
void replyCoolingDown() {
  publish("ack", "{\"status\":\"cooling_down\",\"remainingSeconds\":" +
                     String(session::snapshot().remainingSeconds) + "}",
          false);
}

void handleStartCommand(const String& payload) {
  long sessionId = json::readNumber(payload, "sessionId", -1);
  long durationSeconds = json::readNumber(payload, "durationSeconds", 0);
  long startDelaySeconds = json::readNumber(payload, "startDelaySeconds", 0);

  switch (session::start(sessionId, durationSeconds, startDelaySeconds)) {
    case session::StartResult::BUSY:
      replyToCommand("session_in_progress");
      return;
    case session::StartResult::COOLING_DOWN:
      replyCoolingDown();
      return;
    case session::StartResult::INVALID_DURATION:
      replyToCommand("invalid_duration");
      return;
    case session::StartResult::ACCEPTED:
      replyToCommand("started");
      publishStatus();
      return;
  }
}

void handleRelayTestCommand(const String& payload) {
  long seconds = json::readNumber(payload, "durationSeconds", RELAY_TEST_SECONDS);

  switch (session::startRelayTest(seconds)) {
    case session::StartResult::BUSY:
      replyToCommand("session_in_progress");
      return;
    case session::StartResult::COOLING_DOWN:
      replyCoolingDown();
      return;
    default:
      break;
  }

  replyToCommand("relay_test_started");
  publishStatus();
}

/**
 * Corpo: {"token":"...","ssid":"REDE","password":"senha","bssid":"AA:BB:..."}
 *
 * Espelha handleNetwork() de device_server.cpp: grava na NVS e reconecta. O
 * ack sai *antes* de aplicar a config, pelo mesmo motivo do HTTP — a troca de
 * rede pode derrubar o próprio canal que carregou o comando.
 */
void handleNetworkCommand(const String& payload) {
  String ssid = json::readString(payload, "ssid");
  String password = json::readString(payload, "password");
  String bssid = json::readString(payload, "bssid");

  if (ssid.isEmpty()) {
    replyToCommand("ssid_required");
    return;
  }

  uint8_t parsed[6];
  if (!bssid.isEmpty() && !network_config::parseBssid(bssid, parsed)) {
    replyToCommand("invalid_bssid");
    return;
  }

  bool changed = network_config::save(ssid, password, bssid);
  publish("ack", "{\"status\":\"network_saved\",\"changed\":" +
                     String(changed ? "true" : "false") + ",\"ssid\":\"" + ssid + "\"}",
          false);

  if (changed) {
    network::applyConfig();
  }
}

/**
 * Corpo: {"token":"...","host":"...","port":1883,"username":"...","password":"..."}
 *
 * Espelha handleMqtt() de device_server.cpp: grava o broker na NVS e força
 * reconexão. O ack sai antes de aplicar, mesmo motivo do /network — embora
 * aqui a troca de broker não derrube o canal que carregou o comando (o ack
 * já foi publicado no broker atual antes de mqtt::applyConfig() desconectar).
 */
void handleMqttCommand(const String& payload) {
  String host = json::readString(payload, "host");
  long port = json::readNumber(payload, "port", 0);
  String username = json::readString(payload, "username");
  String password = json::readString(payload, "password");

  if (host.isEmpty()) {
    replyToCommand("host_required");
    return;
  }

  if (port < 0 || port > 65535) {
    replyToCommand("invalid_port");
    return;
  }

  bool changed = mqtt_config::save(host, static_cast<uint16_t>(port), username, password);
  publish("ack", "{\"status\":\"mqtt_saved\",\"changed\":" +
                     String(changed ? "true" : "false") + ",\"host\":\"" + host + "\"}",
          false);

  if (changed) {
    applyConfig();
  }
}

/**
 * Corpo: {"token":"...","active":true}
 *
 * Espelha handlePower() de device_server.cpp: liga/desliga o relé de corte de
 * energia do painel.
 */
void handlePowerCommand(const String& payload) {
  bool active = json::readBool(payload, "active", true);

  chair_state::setActive(active);
  power_relay::apply(active);

  publish("ack", "{\"status\":\"power_applied\",\"active\":" +
                     String(active ? "true" : "false") + "}",
          false);
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  String body;
  body.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    body += (char)payload[i];
  }

  // Só o último nível do tópico importa: fastrelax/chairs/<mac>/cmd/start.
  String command = String(topic);
  int lastSlash = command.lastIndexOf('/');
  if (lastSlash >= 0) {
    command = command.substring(lastSlash + 1);
  }

  if (!authorized(body)) {
    Serial.printf("mqtt: comando %s recusado (token invalido)\n", command.c_str());
    replyToCommand("unauthorized");
    return;
  }

  if (command == "start") {
    handleStartCommand(body);
  } else if (command == "stop") {
    session::stop("comando mqtt");
    replyToCommand("stopped");
    publishStatus();
  } else if (command == "relay-test") {
    handleRelayTestCommand(body);
  } else if (command == "network") {
    handleNetworkCommand(body);
  } else if (command == "mqtt") {
    handleMqttCommand(body);
  } else if (command == "power") {
    handlePowerCommand(body);
  } else {
    Serial.printf("mqtt: comando desconhecido: %s\n", command.c_str());
  }
}

/**
 * Conexão com Last Will: se a cadeira sumir sem avisar, o broker publica
 * "offline" no lugar do dispositivo. É o que daria ao backend uma queda
 * detectada em segundos, contra os três minutos que o heartbeat HTTP leva.
 */
bool connect() {
  // Relê a NVS a cada tentativa, igual wifi_link.cpp faz com a rede: se o
  // backend gravou host/usuário/senha novos enquanto a cadeira estava sem
  // broker, a próxima tentativa já usa a configuração nova, sem reiniciar.
  mqtt_config::Credentials creds = mqtt_config::current();

  String base = baseTopic();
  String willTopic = base + "/online";
  String clientId = String(DEVICE_HOSTNAME) + "-" + deviceId();

  Serial.printf("mqtt: conectando em %s:%d...\n", creds.host.c_str(), creds.port);

  brokerHost = creds.host;
  client.setServer(brokerHost.c_str(), creds.port);
  bool connected = client.connect(clientId.c_str(), creds.username.c_str(),
                                  creds.password.c_str(), willTopic.c_str(), MQTT_QOS,
                                  true, "offline");

  if (!connected) {
    Serial.printf("mqtt: falhou (state %d)\n", client.state());
    return false;
  }

  client.publish(willTopic.c_str(), "online", true);
  String commandTopic = base + "/cmd/#";
  client.subscribe(commandTopic.c_str(), MQTT_QOS);

  Serial.printf("mqtt: conectado; ouvindo %s\n", commandTopic.c_str());
  publishStatus();
  return true;
}

}  // namespace

void begin() {
#if MQTT_USE_TLS
#ifdef MQTT_ROOT_CA
  transport.setCACert(MQTT_ROOT_CA);
#else
  // Sem a raiz configurada não há como conferir o certificado do broker. Serve
  // para subir o canal num teste rápido, não para produção: sem validação, um
  // intermediário na rede pode se passar pelo broker.
  transport.setInsecure();
  Serial.println("mqtt: TLS sem validacao de certificado (defina MQTT_ROOT_CA)");
#endif
#endif

  // setServer real acontece em connect(), a cada tentativa, com o host/porta
  // que estiverem valendo na NVS naquele momento.
  client.setCallback(onMessage);
  client.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
  // O padrão da biblioteca são 256 bytes, folga pequena demais para o payload
  // de status crescer sem susto.
  client.setBufferSize(512);
}

void loop() {
  if (!network::isConnected()) {
    return;
  }

  if (!client.connected()) {
    unsigned long now = millis();
    if (now - lastAttemptMs < MQTT_RETRY_MS) {
      return;
    }
    lastAttemptMs = now;
    // Reconexão não bloqueia: falhou, tenta de novo no próximo intervalo, e a
    // sessão em curso segue contando sozinha.
    connect();
    return;
  }

  client.loop();

  unsigned long now = millis();
  if (now - lastStatusMs >= MQTT_STATUS_INTERVAL_MS) {
    lastStatusMs = now;
    publishStatus();
  }
}

bool isConnected() {
  return client.connected();
}

void applyConfig() {
  Serial.println("mqtt: aplicando configuracao de broker recebida");
  if (client.connected()) {
    client.disconnect();
  }
  // Próximo loop() já tenta de novo, sem esperar MQTT_RETRY_MS: a config
  // acabou de chegar, não faz sentido segurar a reconexão.
  lastAttemptMs = 0;
}

bool publishStatus() {
  session::Snapshot state = session::snapshot();

  String payload = "{";
  payload += "\"phase\":\"" + String(state.phaseName) + "\",";
  payload += "\"remainingSeconds\":" + String(state.remainingSeconds) + ",";
  payload += "\"sessionId\":" + String(state.sessionId) + ",";
  payload += "\"chairOn\":" + String(state.chairOn ? "true" : "false") + ",";
  payload += "\"mac\":\"" + network::macAddress() + "\",";
  payload += "\"ip\":\"" + network::ipAddress() + "\",";
  payload += "\"rssi\":" + String(network::signalStrength()) + ",";
  payload += "\"uptimeSeconds\":" + String(millis() / 1000);
  payload += "}";

  return publish("status", payload, true);
}

bool publish(const char* subtopic, const String& payload, bool retained) {
  if (!client.connected()) {
    return false;
  }
  String topic = baseTopic() + "/" + subtopic;
  return client.publish(topic.c_str(), payload.c_str(), retained);
}

}  // namespace mqtt

#else  // MQTT_ENABLED

// Com o transporte desligado o firmware não carrega a biblioteca nem abre
// socket: as chamadas em main.cpp existem para que ligar MQTT_ENABLED seja a
// única mudança necessária depois.

namespace mqtt {

void begin() {}
void loop() {}
bool isConnected() { return false; }
void applyConfig() {}
bool publishStatus() { return false; }
bool publish(const char*, const String&, bool) { return false; }

}  // namespace mqtt

#endif  // MQTT_ENABLED
