// =========================================================================
// FastRelax — controlador da cadeira
//
// O ESP32 entra na rede como cliente e passa a ser comandado pelo backend:
//   backend -> POST /start  liga o relé pela duração informada
//   backend -> POST /stop   desliga antes do previsto
//   ESP32   -> POST /chairs/heartbeat  avisa que está vivo e informa o IP
//
// Este arquivo só faz a montagem. Cada responsabilidade vive no seu módulo:
//
//   relay/     acionamento do pino que aperta o botão da cadeira
//   power/     relé de corte de energia do painel (nível contínuo, não pulso)
//   state/     ativado/desativado persistido na NVS, para o corte sobreviver a reset
//   session/   máquina de estados do tempo de uso
//   network/   enlace (cabo preferido, Wi-Fi de reserva) e reconexão
//   backend/   heartbeat com o fastrelax-api
//   api/       servidor HTTP do dispositivo e página local
//   mqtt/      transporte alternativo ao HTTP (preparado, desligado)
//   util/      leitura de JSON e banner de identificação
//
// A contagem do tempo é local, e isso é deliberado: se a rede cair depois do
// /start, o relé precisa desligar sozinho. O backend também conta, mas apenas
// para reconciliar o estado da sessão no banco — quem garante o desligamento
// físico é o firmware.
// =========================================================================

#include <Arduino.h>

#include "api/device_server.h"
#include "backend/backend_client.h"
#include "mqtt/mqtt_client.h"
#include "network/network_manager.h"
#include "power/power_relay.h"
#include "relay/relay.h"
#include "session/session_manager.h"
#include "state/chair_state.h"
#include "util/identity.h"

namespace {

void onNetworkLost() {
  // Nada a desfazer: a sessão em andamento continua contando e o relé desliga
  // na hora certa mesmo sem rede.
}

void onNetworkBack() {
  // O DHCP pode ter entregado outro endereço; o heartbeat avisa o backend, e o
  // serial mostra para quem estiver acompanhando.
  identity::print("CADEIRA RECONECTADA");
  backend::sendHeartbeat();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(800);

  relay::begin();
  session::begin();

  // Antes de qualquer rede: uma queda de energia não pode deixar o painel
  // ligado numa cadeira que a Physical desativou.
  chair_state::begin();
  power_relay::begin();

  network::setEventHandlers(onNetworkLost, onNetworkBack);
  network::begin();
  if (network::isConnected()) {
    identity::print("CADEIRA CONECTADA");
  }

  device_api::begin();
  mqtt::begin();
  backend::begin();
}

void loop() {
  device_api::loop();
  network::loop();
  backend::loop();
  mqtt::loop();
  session::loop();
}
