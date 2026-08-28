#include "relay/relay.h"

#include <Arduino.h>

#include "config.h"

namespace relay {
namespace {

bool pulsing = false;
unsigned long pulseStartedMs = 0;

}  // namespace

void begin() {
  pinMode(RELAY_PIN, OUTPUT);
  // Antes de qualquer coisa: contato aberto. Um reset no meio do pulso não
  // pode deixar o botão da cadeira pressionado.
  release();
}

void pulse() {
  // Pulso dentro de pulso reinicia a contagem em vez de emendar os dois: dois
  // toques colados no botão a cadeira leria como um só.
  pulsing = true;
  pulseStartedMs = millis();
  digitalWrite(RELAY_PIN, RELAY_ON);
}

void loop() {
  if (!pulsing) {
    return;
  }
  if (millis() - pulseStartedMs < RELAY_PULSE_MS) {
    return;
  }
  release();
}

bool isPulsing() {
  return pulsing;
}

void release() {
  pulsing = false;
  digitalWrite(RELAY_PIN, RELAY_OFF);
}

}  // namespace relay
