#include "power/power_relay.h"

#include <Arduino.h>

#include "config.h"
#include "state/chair_state.h"

namespace power_relay {

void begin() {
  pinMode(POWER_RELAY_PIN, OUTPUT);
  // chair_state::begin() já rodou antes disto (ver main.cpp) — aplica o que
  // estava salvo na NVS antes de qualquer tentativa de rede.
  apply(chair_state::isActive());
}

void apply(bool active) {
  digitalWrite(POWER_RELAY_PIN, active ? POWER_RELAY_OFF : POWER_RELAY_ON);
}

}  // namespace power_relay
