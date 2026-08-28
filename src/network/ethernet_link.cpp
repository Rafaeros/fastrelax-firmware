#include "network/ethernet_link.h"

#include "config.h"

#if ETHERNET_ENABLED
#include <ETH.h>
#include <WiFi.h>  // WiFiEvent_t / eventos do stack de rede, compartilhados com o ETH
#endif

const char* EthernetLink::name() const {
  return "ethernet";
}

#if ETHERNET_ENABLED

namespace {

// O ETH sobe por eventos, não por polling: guardar o estado aqui evita chamar
// ETH.linkUp() antes de o driver existir.
volatile bool ethConnected = false;

void onNetworkEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(DEVICE_HOSTNAME);
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ethConnected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      ethConnected = false;
      break;
    default:
      break;
  }
}

}  // namespace

void EthernetLink::begin() {
  Serial.println("subindo enlace ethernet...");
  WiFi.onEvent(onNetworkEvent);

  ETH.begin(ETH_PHY_ADDRESS, ETH_PHY_POWER_PIN, ETH_PHY_MDC_PIN,
            ETH_PHY_MDIO_PIN, ETH_PHY_TYPE, ETH_CLOCK_MODE);

  unsigned long deadline = millis() + ETH_CONNECT_TIMEOUT_MS;
  while (!ethConnected && millis() < deadline) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  linkUp_ = ethConnected;
  if (!linkUp_) {
    Serial.println("ethernet sem enlace; confira o cabo e a alimentacao do PHY");
    Serial.printf("MAC address: %s\n", macAddress().c_str());
  }
}

void EthernetLink::loop() {
  // O driver reconecta sozinho pelos eventos; aqui só refletimos o estado.
  linkUp_ = ethConnected;
}

bool EthernetLink::isConnected() const {
  return linkUp_;
}

String EthernetLink::macAddress() const {
  return ETH.macAddress();
}

String EthernetLink::ipAddress() const {
  return ETH.localIP().toString();
}

#else  // ETHERNET_ENABLED

void EthernetLink::begin() {
  Serial.println("ethernet desabilitada (ETHERNET_ENABLED = 0 em config.h)");
}

void EthernetLink::loop() {}

bool EthernetLink::isConnected() const {
  return false;
}

String EthernetLink::macAddress() const {
  return String("00:00:00:00:00:00");
}

String EthernetLink::ipAddress() const {
  return String("0.0.0.0");
}

#endif  // ETHERNET_ENABLED

long EthernetLink::signalStrength() const {
  return 0;
}
