#pragma once

#include "network/network_link.h"

// =========================================================================
// ETHERNET — CABO COM W5500
//
// Enlace preferido quando ETHERNET_ENABLED está ligado em config.h. O Wi-Fi
// não sai de cena: fica de reserva, e quem arbitra entre os dois é o
// network_manager. O resto do firmware não muda, porque heartbeat, servidor
// HTTP e página local falam com NetworkLink, não com o meio.
//
// O ESP32 desta placa é um DevKit comum, sem PHY RMII embarcado, então o cabo
// entra por um módulo W5500 externo ligado ao barramento SPI. O driver do
// W5500 é registrado direto no lwIP (esp_eth + esp_netif), e não pela
// biblioteca Ethernet.h avulsa: é isso que faz WebServer e HTTPClient
// continuarem funcionando sem alteração, e é isso que permite os dois enlaces
// coexistirem com o cabo ganhando por prioridade de rota.
//
// Com ETHERNET_ENABLED desligado a classe continua compilando, mas se comporta
// como enlace permanentemente fora do ar: nada aqui é exercitado por engano.
// =========================================================================

class EthernetLink : public NetworkLink {
 public:
  const char* name() const override;
  void begin() override;
  void loop() override;
  bool isConnected() const override;
  /** MAC de Ethernet derivado do eFuse: o W5500 não traz endereço de fábrica. */
  String macAddress() const override;
  String ipAddress() const override;
  /** Cabo não tem RSSI: sempre 0. */
  long signalStrength() const override;

 private:
  bool linkUp_ = false;
};
