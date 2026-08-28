#pragma once

#include "network/network_link.h"

/** Enlace padrão da cadeira: estação Wi-Fi com reconexão automática. */
class WifiLink : public NetworkLink {
 public:
  const char* name() const override;
  void begin() override;
  void loop() override;
  void applyNetworkConfig() override;
  bool isConnected() const override;
  String macAddress() const override;
  String ipAddress() const override;
  long signalStrength() const override;

 private:
  unsigned long lastAttemptMs_ = 0;
};
