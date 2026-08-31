#include "network/network_manager.h"

#include <esp_mac.h>

#include "config.h"
#include "network/ethernet_link.h"
#include "network/network_link.h"
#include "network/wifi_link.h"

namespace network {
namespace {

WifiLink wifiLink;
EthernetLink ethernetLink;

// Com ETHERNET_ENABLED os dois enlaces sobem juntos: o cabo é o caminho
// preferido e o Wi-Fi fica de reserva quente, associado e ocioso. Não é
// desperdício — é o que faz a queda do cabo custar um loop() em vez dos vinte
// segundos de uma associação começando do zero, com a cadeira fora do ar.
//
// O desvio do tráfego em si não acontece aqui: quem faz isso é a prioridade de
// rota do lwIP, configurada em ethernet_link.cpp. Este ponteiro só registra
// qual meio está valendo, para o log e para o IP que vai no heartbeat.
NetworkLink* active = &wifiLink;

Event disconnectedHandler = nullptr;
Event reconnectedHandler = nullptr;

// Começa como conectado para o primeiro loop() não anunciar reconexão de uma
// rede que nunca caiu.
bool wasConnected = true;

/** Cabo enquanto houver cabo; Wi-Fi no resto do tempo. */
NetworkLink* preferredLink() {
#if ETHERNET_ENABLED
  if (ethernetLink.isConnected()) {
    return &ethernetLink;
  }
#endif
  return &wifiLink;
}

}  // namespace

void setEventHandlers(Event onDisconnected, Event onReconnected) {
  disconnectedHandler = onDisconnected;
  reconnectedHandler = onReconnected;
}

void begin() {
#if ETHERNET_ENABLED
  ethernetLink.begin();

  if (ethernetLink.isConnected()) {
    // Cabo de pé: o Wi-Fi sobe assim mesmo, mas sem segurar o boot por uma
    // rede que talvez nunca seja usada.
    wifiLink.startAsync();
  } else {
    // Sem cabo o Wi-Fi deixa de ser reserva e vira o enlace de verdade, então
    // vale esperar por ele antes de seguir: o heartbeat e o cadastro da cadeira
    // dependem de haver rede logo no boot.
    Serial.println("ethernet indisponivel; caindo para wi-fi");
    wifiLink.begin();
  }
#else
  wifiLink.begin();
#endif

  active = preferredLink();
  wasConnected = active->isConnected();

  // Sem isto o serial não deixa claro por onde a cadeira está falando: os LEDs
  // do RJ45 acendem só de ter energia no W5500, mesmo com o firmware ignorando
  // o cabo, então eles não servem de prova.
#if ETHERNET_ENABLED
  Serial.printf("enlaces: ethernet=%s | wi-fi=%s\n",
                ethernetLink.isConnected() ? "no ar" : "fora",
                wifiLink.isConnected() ? "no ar" : "fora");
#endif
  Serial.printf("enlace ativo: %s (%s)\n", active->name(),
                wasConnected ? active->ipAddress().c_str() : "sem IP");
}

void applyConfig() {
  // Só o Wi-Fi tem o que reaplicar: SSID e senha não dizem nada ao cabo, e
  // derrubar o enlace cabeado por causa de uma troca de rede sem fio deixaria a
  // cadeira offline sem motivo.
  wifiLink.applyNetworkConfig();
  // O estado é reavaliado no loop seguinte. Derrubar `wasConnected` aqui faria
  // o firmware anunciar uma queda que foi pedida, não uma falha.
}

void loop() {
#if ETHERNET_ENABLED
  ethernetLink.loop();
#endif
  wifiLink.loop();

  NetworkLink* next = preferredLink();
  bool switched = next != active;
  if (switched) {
    Serial.printf("enlace ativo: %s -> %s\n", active->name(), next->name());
    active = next;
  }

  bool connected = active->isConnected();
  if (!switched && connected == wasConnected) {
    return;
  }

  bool previously = wasConnected;
  wasConnected = connected;

  if (!connected) {
    if (previously) {
      Serial.printf("%s caiu; reconectando em background\n", active->name());
      if (disconnectedHandler != nullptr) {
        disconnectedHandler();
      }
    }
    return;
  }

  // Voltou a ter rede, ou trocou de meio sem ficar sem rede. Nos dois casos o
  // IP mudou, e o backend só descobre isso pelo heartbeat que o handler dispara.
  if (!previously || switched) {
    if (reconnectedHandler != nullptr) {
      reconnectedHandler();
    }
  }
}

bool isConnected() {
  return active->isConnected();
}

String macAddress() {
  // Identidade da cadeira no backend, e por isso não pode mudar quando o meio
  // muda: cabo e Wi-Fi têm MACs diferentes, e o cadastro é feito por MAC. Uma
  // cadeira que troca de MAC no failover viraria uma cadeira desconhecida no
  // meio do expediente. Fixamos o MAC de estação Wi-Fi, que é o mesmo valor que
  // a cadeira já reportava antes de existir enlace cabeado.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  return String(buffer);
}

String ipAddress() {
  return active->ipAddress();
}

long signalStrength() {
  return active->signalStrength();
}

const char* linkName() {
  return active->name();
}

}  // namespace network
