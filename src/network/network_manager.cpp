#include "network/network_manager.h"

#include "config.h"
#include "network/ethernet_link.h"
#include "network/network_link.h"
#include "network/wifi_link.h"

namespace network {
namespace {

WifiLink wifiLink;
EthernetLink ethernetLink;

// Um enlace por vez. Trocar o meio é mexer só nesta linha (e no config.h).
NetworkLink& link = ETHERNET_ENABLED ? (NetworkLink&)ethernetLink : (NetworkLink&)wifiLink;

Event disconnectedHandler = nullptr;
Event reconnectedHandler = nullptr;

// Começa como conectado para o primeiro loop() não anunciar reconexão de uma
// rede que nunca caiu.
bool wasConnected = true;

}  // namespace

void setEventHandlers(Event onDisconnected, Event onReconnected) {
  disconnectedHandler = onDisconnected;
  reconnectedHandler = onReconnected;
}

void begin() {
  link.begin();
  wasConnected = link.isConnected();
}

void applyConfig() {
  link.applyNetworkConfig();
  // O estado é reavaliado no loop seguinte. Derrubar `wasConnected` aqui faria
  // o firmware anunciar uma queda que foi pedida, não uma falha.
}

void loop() {
  link.loop();

  bool connected = link.isConnected();
  if (connected == wasConnected) {
    return;
  }

  wasConnected = connected;
  if (!connected) {
    Serial.printf("%s caiu; reconectando em background\n", link.name());
    if (disconnectedHandler != nullptr) {
      disconnectedHandler();
    }
    return;
  }

  if (reconnectedHandler != nullptr) {
    reconnectedHandler();
  }
}

bool isConnected() {
  return link.isConnected();
}

String macAddress() {
  return link.macAddress();
}

String ipAddress() {
  return link.ipAddress();
}

long signalStrength() {
  return link.signalStrength();
}

const char* linkName() {
  return link.name();
}

}  // namespace network
