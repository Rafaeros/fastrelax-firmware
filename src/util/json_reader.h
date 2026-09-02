#pragma once

#include <Arduino.h>

// =========================================================================
// LEITURA DE JSON
//
// Os corpos trocados com o backend têm três campos numéricos no máximo, então
// extrair na mão evita uma dependência externa. Aceita apenas inteiros — que é
// tudo que o backend envia.
// =========================================================================

namespace json {

/** Valor inteiro de `key`, ou `fallback` quando a chave não existe ou não é número. */
long readNumber(const String& body, const char* key, long fallback);

/**
 * Valor de texto de `key`, ou string vazia quando a chave não existe.
 *
 * Entrou com a configuração de rede: SSID e senha são texto, e chegam do
 * backend pelo POST /network. Trata as sequências de escape que o JSON exige
 * — `\"` e `\` — porque senha de Wi-Fi costuma ter os dois.
 */
String readString(const String& body, const char* key);

/**
 * Valor booleano de `key` (`true`/`false`, sem aspas), ou `fallback` quando a
 * chave não existe ou não é um dos dois literais.
 *
 * Entrou com POST /power e com a leitura da resposta do heartbeat — ambos
 * trafegam `"active":true`/`false`, o único booleano que o firmware precisa
 * ler (os que ele só escreve, como `"saved":true`, não passam por aqui).
 */
bool readBool(const String& body, const char* key, bool fallback);

}  // namespace json
