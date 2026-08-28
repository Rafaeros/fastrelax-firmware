#pragma once

#include <Arduino.h>

// =========================================================================
// ENLACE DE REDE
//
// Contrato que o resto do firmware enxerga. Trocar Wi-Fi por Ethernet é
// escolher outra implementação em network_manager.cpp — heartbeat, servidor
// HTTP e página local continuam iguais.
// =========================================================================

class NetworkLink {
 public:
  virtual ~NetworkLink() = default;

  /** Nome do enlace no log ("wi-fi", "ethernet"). */
  virtual const char* name() const = 0;

  /** Sobe o enlace. Pode bloquear até o timeout de conexão inicial. */
  virtual void begin() = 0;

  /** Reconexão em background. Nunca bloqueia: a sessão em curso não pode parar. */
  virtual void loop() = 0;

  /**
   * Reaplica a configuração de rede recebida do backend.
   *
   * Só o Wi-Fi tem o que fazer aqui; o enlace cabeado ignora, porque SSID e
   * senha não significam nada para ele. Está no contrato, e não só no WifiLink,
   * para o servidor HTTP do dispositivo não precisar saber qual meio está ativo.
   */
  virtual void applyNetworkConfig() {}

  virtual bool isConnected() const = 0;

  /** Identidade do dispositivo no backend — não muda. */
  virtual String macAddress() const = 0;

  virtual String ipAddress() const = 0;

  /** RSSI em dBm. Enlace cabeado devolve 0: a métrica não se aplica. */
  virtual long signalStrength() const = 0;
};
