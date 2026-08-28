#pragma once

#include <Arduino.h>

// =========================================================================
// CONFIGURAÇÃO DE REDE PERSISTIDA
//
// SSID, senha e BSSID são armazenados na NVS do ESP32.
//
// Em uma placa nova, a NVS "fr-net" é criada automaticamente e, enquanto
// não houver configuração salva, os valores de config.h são utilizados.
//
// A configuração recebida pelo backend é persistida na NVS e sobrevive a:
//   - reset
//   - reinicialização
//   - atualização/regravação do firmware
//
// Para apagar a configuração persistida, use clear() ou apague a partição
// NVS explicitamente.
// =========================================================================

namespace network_config {

struct Credentials {
  String ssid;
  String password;

  /**
   * MAC do ponto de acesso, em:
   *
   *   AA:BB:CC:DD:EE:FF
   *
   * Vazio significa que o ESP32 pode escolher qualquer AP disponível
   * com o mesmo SSID.
   */
  String bssid;
};

/**
 * Lê a configuração persistida.
 *
 * Se ainda não existir configuração na NVS, retorna os valores
 * definidos em config.h.
 */
Credentials current();

/**
 * Retorna true quando existe uma configuração de rede salva na NVS.
 *
 * Retorna false para uma cadeira nova que ainda utiliza config.h.
 */
bool isProvisioned();

/**
 * Salva a configuração de rede na NVS.
 *
 * Retorna true somente quando algum valor realmente mudou.
 */
bool save(
    const String& ssid,
    const String& password,
    const String& bssid
);

/**
 * Remove toda a configuração persistida da rede.
 *
 * Depois disso, current() volta a utilizar os valores de config.h.
 */
void clear();

/**
 * Converte um BSSID textual para os 6 bytes utilizados pelo WiFi.begin().
 *
 * Aceita:
 *   AA:BB:CC:DD:EE:FF
 *   AA-BB-CC-DD-EE-FF
 *
 * Retorna false se o formato for inválido.
 */
bool parseBssid(
    const String& text,
    uint8_t out[6]
);

}  // namespace network_config