#include "network/wifi_link.h"

#include <WiFi.h>

#include "config.h"
#include "network/network_config.h"

namespace {

/**
 * Sobe a conexão com o que estiver gravado.
 *
 * <p>
 * Com BSSID válido, o ESP32 vai direto naquele ponto de acesso; sem ele,
 * escolhe o de melhor sinal no SSID. Fixar importa em planta com vários APs no
 * mesmo nome de rede — sem isso a cadeira pode grudar num ponto distante e
 * ficar com sinal ruim tendo um AP a três metros.
 */
void connectWith(const network_config::Credentials& creds) {
  uint8_t bssid[6];

  if (!creds.bssid.isEmpty() && network_config::parseBssid(creds.bssid, bssid)) {
    WiFi.begin(creds.ssid.c_str(), creds.password.c_str(), 0, bssid);
    return;
  }

  // BSSID vazio ou malformado não impede a conexão: perde-se a fixação do AP,
  // não a rede. Uma cadeira offline por causa de um MAC digitado errado seria
  // pior que uma cadeira num AP subótimo.
  WiFi.begin(creds.ssid.c_str(), creds.password.c_str());
}

}  // namespace

const char* WifiLink::name() const {
  return "wi-fi";
}

void WifiLink::begin() {
  network_config::Credentials creds = network_config::current();

  Serial.printf("conectando em %s%s...\n", creds.ssid.c_str(),
                network_config::isProvisioned() ? "" : " (padrao de fabrica)");
  if (!creds.bssid.isEmpty()) {
    Serial.printf("ponto de acesso fixado em %s\n", creds.bssid.c_str());
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  // Sem isto o ESP32 desliga o rádio entre pacotes e demora a responder aos
  // comandos do backend.
  WiFi.setSleep(false);
  connectWith(creds);

  unsigned long deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();

  if (!isConnected()) {
    Serial.println("nao foi possivel conectar; tentando de novo em background");
    // Sem rede o IP ainda não existe, mas o MAC já vale para adiantar o cadastro.
    Serial.printf("MAC address: %s\n", macAddress().c_str());
  }
}

void WifiLink::loop() {
  if (isConnected()) {
    return;
  }

  unsigned long now = millis();
  if (now - lastAttemptMs_ < WIFI_RETRY_MS) {
    return;
  }

  lastAttemptMs_ = now;
  WiFi.disconnect();
  // Relê a NVS a cada tentativa: se a configuração chegou enquanto a cadeira
  // estava fora do ar, a próxima tentativa já usa a rede nova, sem reiniciar.
  connectWith(network_config::current());
}

void WifiLink::applyNetworkConfig() {
  Serial.println("aplicando configuracao de rede recebida");
  WiFi.disconnect();
  connectWith(network_config::current());
  lastAttemptMs_ = millis();
}

bool WifiLink::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WifiLink::macAddress() const {
  return WiFi.macAddress();
}

String WifiLink::ipAddress() const {
  return WiFi.localIP().toString();
}

long WifiLink::signalStrength() const {
  return WiFi.RSSI();
}
