#include "mqtt/mqtt_config.h"

#include <Preferences.h>

#include "config.h"

namespace mqtt_config {
namespace {

// Namespace próprio, separado do "fr-net" do Wi-Fi: são credenciais de
// sistemas diferentes, e limpar uma rede (network_config::clear()) não deve
// arrastar o broker junto.
constexpr const char* kNamespace = "fr-mqtt";

constexpr const char* kHostKey = "host";
constexpr const char* kPortKey = "port";
constexpr const char* kUsernameKey = "user";
constexpr const char* kPasswordKey = "pass";

Preferences prefs;

/** Mesmo padrão de network_config.cpp: abre a NVS e garante o fechamento. */
struct Scope {
  bool opened = false;

  explicit Scope(bool readOnly) {
    opened = prefs.begin(kNamespace, readOnly);

    if (!opened) {
      Serial.printf("ERRO: nao foi possivel abrir NVS namespace '%s'\n", kNamespace);
    }
  }

  ~Scope() {
    if (opened) {
      prefs.end();
    }
  }
};

}  // namespace

Credentials current() {
  Credentials stored;

  // false, não true: com readOnly=true o Preferences falha ao abrir um
  // namespace que ainda não existe — o caso normal de um device nunca
  // provisionado — e isto imprimiria o ERRO do Scope a cada chamada, nada
  // sutil já que current() é lido a cada tentativa de conexão do MQTT.
  Scope scope(false);

  if (!scope.opened || !prefs.isKey(kHostKey) || prefs.getString(kHostKey, "").isEmpty()) {
    // Sem NVS aberta, ou sem host gravado: device ainda fala com o broker
    // padrão de fábrica.
    stored.host = MQTT_HOST;
    stored.port = MQTT_PORT;
    stored.username = MQTT_USERNAME;
    stored.password = MQTT_PASSWORD;
    return stored;
  }

  stored.host = prefs.getString(kHostKey, "");
  stored.port = prefs.getUShort(kPortKey, MQTT_PORT);
  stored.username = prefs.getString(kUsernameKey, "");
  stored.password = prefs.getString(kPasswordKey, "");
  return stored;
}

bool isProvisioned() {
  Scope scope(false);

  if (!scope.opened) {
    return false;
  }

  return prefs.isKey(kHostKey) && !prefs.getString(kHostKey, "").isEmpty();
}

bool save(
    const String& host,
    uint16_t port,
    const String& username,
    const String& password
) {
  if (host.isEmpty()) {
    return false;
  }

  Scope scope(false);

  if (!scope.opened) {
    Serial.println("ERRO: nao foi possivel abrir NVS para salvar configuracao de mqtt");
    return false;
  }

  // Porta 0 pede pra manter a atual: existe pra deixar trocar só usuário e
  // senha do backend sem precisar repetir a porta a cada chamada.
  uint16_t effectivePort = port != 0 ? port : prefs.getUShort(kPortKey, MQTT_PORT);

  bool changed = prefs.getString(kHostKey, "") != host ||
                 prefs.getUShort(kPortKey, MQTT_PORT) != effectivePort ||
                 prefs.getString(kUsernameKey, "") != username ||
                 prefs.getString(kPasswordKey, "") != password;

  if (!changed) {
    return false;
  }

  bool hostOk = prefs.putString(kHostKey, host) > 0;
  bool portOk = prefs.putUShort(kPortKey, effectivePort) > 0;
  bool userOk = prefs.putString(kUsernameKey, username) > 0;
  // putString com string vazia grava 0 bytes de payload mas ainda é uma
  // gravação válida (senha vazia é uma escolha legítima, ex.: broker sem
  // auth) — por isso não trata isto como falha.
  prefs.putString(kPasswordKey, password);

  if (!hostOk || !portOk || !userOk) {
    Serial.println("ERRO: falha ao gravar configuracao de mqtt na NVS");
    return false;
  }

  Serial.println("NVS: configuracao de mqtt salva");
  return true;
}

void clear() {
  Scope scope(false);

  if (!scope.opened) {
    Serial.println("ERRO: nao foi possivel abrir NVS para limpar configuracao de mqtt");
    return;
  }

  prefs.clear();
  Serial.println("NVS: configuracao de mqtt removida");
}

}  // namespace mqtt_config
