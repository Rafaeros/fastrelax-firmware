#include "util/device_identity.h"

#include <Preferences.h>
#include <esp_system.h>

namespace device_identity {
namespace {

// NVS separada da rede (fr-net): identidade do dispositivo é um assunto
// diferente, e um `clear()` de rede não deve levar o token junto.
constexpr const char* kNamespace = "fr-dev";
constexpr const char* kTokenKey = "token";

// 32 bytes = 256 bits, hex-codificados em 64 caracteres. Mesma ordem de
// grandeza dos tokens JWT/AES do backend — força bruta não é caminho viável.
constexpr size_t kTokenBytes = 32;

String cached;

String generate() {
  uint8_t raw[kTokenBytes];
  // RNG de hardware do ESP32 (WiFi/BT precisam dele ligados para entropia de
  // verdade; no boot normal já estão). Não é o rand() de software.
  esp_fill_random(raw, sizeof(raw));

  static const char kHex[] = "0123456789abcdef";
  String out;
  out.reserve(kTokenBytes * 2);
  for (size_t i = 0; i < kTokenBytes; i++) {
    out += kHex[raw[i] >> 4];
    out += kHex[raw[i] & 0x0F];
  }
  return out;
}

}  // namespace

const String& token() {
  if (cached.length() > 0) {
    return cached;
  }

  Preferences prefs;
  bool opened = prefs.begin(kNamespace, false);

  if (opened && prefs.isKey(kTokenKey)) {
    cached = prefs.getString(kTokenKey, "");
  }

  if (cached.length() == 0) {
    cached = generate();

    if (opened) {
      prefs.putString(kTokenKey, cached);
      Serial.println("device_identity: token gerado e gravado na NVS");
    } else {
      // Sem NVS não há como persistir — a cadeira gera um token novo a cada
      // boot e o backend pareia de novo. Funciona, mas todo reset reseta a
      // identidade; melhor que travar o firmware por causa disso.
      Serial.println("device_identity: NVS indisponivel; token valido so ate o proximo boot");
    }
  }

  if (opened) {
    prefs.end();
  }

  return cached;
}

}  // namespace device_identity
