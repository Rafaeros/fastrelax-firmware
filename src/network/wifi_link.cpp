#include "network/wifi_link.h"

#include <WiFi.h>

#include "config.h"
#include "network/network_config.h"

namespace {

// Folga entre desassociar e associar de novo. Não precisa ser exato: é só o
// tempo de o rádio sair do estado de "connecting".
constexpr unsigned long kReconnectSettleMs = 800;

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

void WifiLink::startAsync() {
  network_config::Credentials creds = network_config::current();

  // A origem no log não é decoração: a NVS sobrevive à regravação, então uma
  // cadeira provisionada ignora o config.h para sempre. Sem dizer de onde veio o
  // SSID, quem editou o config.h e não viu efeito conclui que o firmware está
  // quebrado, quando na verdade está fazendo o certo.
  bool provisioned = network_config::isProvisioned();
  Serial.printf("conectando em %s (%s)...\n", creds.ssid.c_str(),
                provisioned ? "gravado na NVS pelo backend" : "padrao de fabrica");
  if (!creds.bssid.isEmpty()) {
    Serial.printf("ponto de acesso fixado em %s (%s)\n", creds.bssid.c_str(),
                  provisioned ? "NVS" : "config.h");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  // Sem isto o ESP32 desliga o rádio entre pacotes e demora a responder aos
  // comandos do backend.
  WiFi.setSleep(false);
  connectWith(creds);
  lastAttemptMs_ = millis();
}

void WifiLink::begin() {
  startAsync();

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
    // Já gastamos a janela do boot esperando: a próxima tentativa é imediata,
    // não daqui a WIFI_CONNECT_TIMEOUT_MS.
    lastAttemptMs_ = 0;
  }
}

void WifiLink::loop() {
  if (isConnected()) {
    retryPending_ = false;
    return;
  }

  unsigned long now = millis();

  // Desassociar e associar são assíncronos: o esp_wifi_disconnect() volta antes
  // de o rádio parar, e um connect emendado nele é recusado com "sta is
  // connecting" — a tentativa se perde inteira, e a seguinte também, para
  // sempre. Por isso a reconexão acontece em duas passadas, com uma folga no
  // meio para o rádio assentar.
  if (retryPending_) {
    if (now - lastAttemptMs_ < kReconnectSettleMs) {
      return;
    }

    retryPending_ = false;
    lastAttemptMs_ = now;
    // Relê a NVS a cada tentativa: se a configuração chegou enquanto a cadeira
    // estava fora do ar, a próxima tentativa já usa a rede nova, sem reiniciar.
    connectWith(network_config::current());
    return;
  }

  // O begin() dado em connectWith() ainda pode estar em andamento — scan,
  // autenticação, associação e DHCP às vezes passam de 10s, principalmente com
  // sinal fraco ou o AP ainda subindo depois de um reboot. Usar uma janela
  // menor aqui derrubava a tentativa no meio do caminho a cada ciclo, para
  // sempre: o boot dava a conexão inicial os WIFI_CONNECT_TIMEOUT_MS cheios,
  // mas o background só dava uma fração disso — por isso conectava no boot
  // mas nunca em background depois de uma queda.
  if (now - lastAttemptMs_ < WIFI_CONNECT_TIMEOUT_MS) {
    return;
  }

  lastAttemptMs_ = now;
  retryPending_ = true;
  WiFi.disconnect();
}

void WifiLink::applyNetworkConfig() {
  Serial.println("aplicando configuracao de rede recebida");
  WiFi.disconnect();
  // Mesmo cuidado do loop(): associar emendado no disconnect é recusado. O
  // connect sai na passada seguinte, já com as credenciais novas da NVS.
  lastAttemptMs_ = millis();
  retryPending_ = true;
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
