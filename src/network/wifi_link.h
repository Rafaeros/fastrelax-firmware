#pragma once

#include "network/network_link.h"

/** Enlace padrão da cadeira: estação Wi-Fi com reconexão automática. */
class WifiLink : public NetworkLink {
 public:
  const char* name() const override;

  /** Sobe a estação e espera até WIFI_CONNECT_TIMEOUT_MS pela associação. */
  void begin() override;

  /**
   * Sobe a estação e volta na hora, sem esperar pela associação.
   *
   * É o que o gerenciador usa quando o Wi-Fi é reserva do cabo: o boot não
   * pode parar 20 segundos por uma rede que talvez nem chegue a ser usada, mas
   * a estação precisa estar associada *antes* de o cabo cair — senão o
   * failover custaria o tempo de associação, com a cadeira fora do ar.
   */
  void startAsync();

  void loop() override;
  void applyNetworkConfig() override;
  bool isConnected() const override;
  String macAddress() const override;
  String ipAddress() const override;
  long signalStrength() const override;

 private:
  unsigned long lastAttemptMs_ = 0;
  /** Desassociou e ainda não reassociou. Ver loop(). */
  bool retryPending_ = false;
};
