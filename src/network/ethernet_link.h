#pragma once

#include "network/network_link.h"

// =========================================================================
// ETHERNET — PREPARADO, AINDA NÃO USADO
//
// A instalação atual é Wi-Fi. Este módulo existe para o dia em que a cadeira
// ficar num ponto com cabo: ligue ETHERNET_ENABLED em config.h, confira os
// pinos do módulo (LAN8720 no ESP32) e o resto do firmware não muda — o
// heartbeat, o servidor HTTP e a página local falam com NetworkLink, não com
// o Wi-Fi.
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
  String macAddress() const override;
  String ipAddress() const override;
  /** Cabo não tem RSSI: sempre 0. */
  long signalStrength() const override;

 private:
  bool linkUp_ = false;
};
