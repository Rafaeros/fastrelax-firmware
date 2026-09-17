#pragma once

#include <Arduino.h>

// =========================================================================
// TOKEN DO DISPOSITIVO
//
// Substitui o antigo DEVICE_TOKEN de config.h — um segredo único, igual em
// todo firmware gravado. Cada cadeira agora sorteia o próprio token no
// primeiro boot (RNG de hardware do ESP32) e grava na NVS; dali em diante é
// sempre o mesmo. O backend pareia com ele no primeiro heartbeat (confiança
// no primeiro contato) — ver ChairService#authenticateDevice no fastrelax-api.
//
// Efeito prático: saber o MAC de uma cadeira (impresso na etiqueta do ESP32)
// não é mais suficiente para assumir a identidade dela perante o backend, e
// vazar o token de uma cadeira não compromete as outras.
// =========================================================================

namespace device_identity {

/** Gera na primeira chamada (e grava na NVS); as seguintes só leem o cache. */
const String& token();

}  // namespace device_identity
