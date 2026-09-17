#pragma once

#include <Arduino.h>

// =========================================================================
// CONFIGURAÇÃO DE MQTT PERSISTIDA
//
// Espelha network_config.h: host, porta, usuário e senha do broker ficam na
// NVS do ESP32 (namespace separado do Wi-Fi). Enquanto não houver
// configuração salva, valem os valores de config.h — por isso todo device sai
// de fábrica falando com o mesmo broker padrão, e só passa a ser dinâmico
// quando o backend grava algo diferente por essa via.
//
// A senha do broker nunca é lida de volta por nenhuma API do firmware: só
// current() (uso interno do cliente MQTT) enxerga o valor. O HTTP local só
// grava, nunca devolve.
// =========================================================================

namespace mqtt_config {

struct Credentials {
  String host;
  uint16_t port = 0;
  String username;
  String password;
};

/**
 * Lê a configuração persistida.
 *
 * Se ainda não existir configuração na NVS, retorna os valores definidos em
 * config.h (MQTT_HOST, MQTT_PORT, MQTT_USERNAME, MQTT_PASSWORD).
 */
Credentials current();

/**
 * Retorna true quando existe uma configuração de MQTT salva na NVS.
 *
 * Retorna false para um device que ainda fala com o broker padrão de
 * config.h.
 */
bool isProvisioned();

/**
 * Salva a configuração de MQTT na NVS.
 *
 * `host` vazio é recusado (retorna false sem gravar) — não existe broker sem
 * endereço. `port` 0 mantém a porta atualmente gravada, ou a de config.h se
 * nunca foi provisionada; existe para permitir trocar só usuário/senha sem
 * repetir a porta.
 */
bool save(
    const String& host,
    uint16_t port,
    const String& username,
    const String& password
);

/**
 * Remove toda a configuração persistida de MQTT.
 *
 * Depois disso, current() volta a utilizar os valores de config.h.
 */
void clear();

}  // namespace mqtt_config
