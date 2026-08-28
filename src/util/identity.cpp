#include "util/identity.h"

#include <Arduino.h>

#include "config.h"
#include "network/network_manager.h"

namespace identity {

void print(const char* header) {
  Serial.println();
  Serial.println("==========================================================");
  Serial.printf("  %s\n", header);
  Serial.println("==========================================================");
  Serial.printf("  MAC address : %s\n", network::macAddress().c_str());
  Serial.printf("  IP na rede  : %s\n", network::ipAddress().c_str());
  Serial.printf("  Enlace      : %s\n", network::linkName());
  Serial.printf("  Porta HTTP  : 80\n");
  Serial.printf("  Hostname    : %s\n", DEVICE_HOSTNAME);
  Serial.println("----------------------------------------------------------");
  Serial.println("  Cadastre o MAC acima em Painel > Cadeiras > Nova cadeira");
  Serial.println("==========================================================");
  Serial.println();
}

}  // namespace identity
