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

#include "network/network_manager.h"
#include "session/session_manager.h"
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
  return payload.substring(firstQuote + 1, lastQuote) == String(DEVICE_TOKEN);
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
  String base = baseTopic();
  String willTopic = base + "/online";
  String clientId = String(DEVICE_HOSTNAME) + "-" + deviceId();

  Serial.printf("mqtt: conectando em %s:%d...\n", MQTT_HOST, MQTT_PORT);

  bool connected = client.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                                  willTopic.c_str(), MQTT_QOS, true, "offline");

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

  client.setServer(MQTT_HOST, MQTT_PORT);
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
bool publishStatus() { return false; }
bool publish(const char*, const String&, bool) { return false; }

}  // namespace mqtt

#endif  // MQTT_ENABLED
