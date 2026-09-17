#include "network/network_config.h"

#include <Preferences.h>

#include "config.h"

namespace network_config {
namespace {

// =========================================================================
// NVS
// =========================================================================

// Nome do namespace.
// A NVS permite no máximo 15 caracteres para o namespace.
constexpr const char* kNamespace = "fr-net";

constexpr const char* kSsidKey = "ssid";
constexpr const char* kPasswordKey = "pass";
constexpr const char* kBssidKey = "bssid";

Preferences prefs;

/**
 * Abre o namespace da NVS e garante que ele seja fechado.
 *
 * IMPORTANTE:
 *
 * Quando readOnly == false, o Preferences pode criar o namespace caso ele
 * ainda não exista.
 *
 * Isso é importante para uma cadeira nova, onde "fr-net" ainda não existe.
 */
struct Scope {
  bool opened = false;

  explicit Scope(bool readOnly) {
    opened = prefs.begin(kNamespace, readOnly);

    if (!opened) {
      Serial.printf(
          "ERRO: nao foi possivel abrir NVS namespace '%s'\n",
          kNamespace
      );
    }
  }

  ~Scope() {
    if (opened) {
      prefs.end();
    }
  }
};

/**
 * Retorna um valor hexadecimal ou -1 quando inválido.
 */
int hexValue(char digit) {
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }

  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }

  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }

  return -1;
}

}  // namespace

// =========================================================================
// CURRENT
// =========================================================================

Credentials current() {
  Credentials stored;

#if WIFI_FORCE_FACTORY
  // Nem abre a NVS: o ponto da escotilha é funcionar mesmo com a NVS gravada
  // para uma rede inalcançável, que é justamente quando não há como apagá-la
  // pela rede.
  Serial.println(
      "WIFI_FORCE_FACTORY: ignorando a NVS e usando config.h"
  );

  stored.ssid = WIFI_SSID;
  stored.password = WIFI_PASSWORD;
  stored.bssid = WIFI_BSSID;

  return stored;
#else
  Scope scope(false);

  if (!scope.opened) {
    Serial.println(
        "NVS indisponivel; usando configuracao de config.h"
    );

    stored.ssid = WIFI_SSID;
    stored.password = WIFI_PASSWORD;
    stored.bssid = WIFI_BSSID;

    return stored;
  }

  // Só tenta ler a chave se ela realmente existir.
  if (prefs.isKey(kSsidKey)) {
    stored.ssid = prefs.getString(kSsidKey, "");
  }

  if (prefs.isKey(kPasswordKey)) {
    stored.password = prefs.getString(kPasswordKey, "");
  }

  if (prefs.isKey(kBssidKey)) {
    stored.bssid = prefs.getString(kBssidKey, "");
  }

  // Cadeira ainda não provisionada.
  // Usa SSID e senha padrão do config.h.
  if (stored.ssid.isEmpty()) {
    stored.ssid = WIFI_SSID;
    stored.password = WIFI_PASSWORD;
  }

  // Sem BSSID gravado — seja porque a cadeira nunca foi provisionada, seja
  // porque foi, mas o backend nunca fixou um ponto de acesso — cai no de
  // fábrica. Só fica de fato vazio (ESP32 escolhe o de melhor sinal) quando
  // nem o config.h define um.
  if (stored.bssid.isEmpty()) {
    stored.bssid = WIFI_BSSID;
  }

  return stored;
#endif
}

// =========================================================================
// IS PROVISIONED
// =========================================================================

bool isProvisioned() {
#if WIFI_FORCE_FACTORY
  // A NVS pode até estar gravada, mas não é ela que está no ar. Responder
  // "provisionada" aqui faria o log e o /status contarem uma rede que a cadeira
  // não está usando.
  return false;
#else
  Scope scope(false);

  if (!scope.opened) {
    return false;
  }

  return prefs.isKey(kSsidKey) &&
         !prefs.getString(kSsidKey, "").isEmpty();
#endif
}

// =========================================================================
// SAVE
// =========================================================================

bool save(
    const String& ssid,
    const String& password,
    const String& bssid
) {
  // false = leitura + escrita.
  //
  // Se o namespace ainda não existir, ele será criado.
  Scope scope(false);

  if (!scope.opened) {
    Serial.println(
        "ERRO: nao foi possivel abrir NVS para salvar configuracao"
    );

    return false;
  }

  // Lê os valores atualmente armazenados.
  String currentSsid =
      prefs.getString(kSsidKey, "");

  String currentPassword =
      prefs.getString(kPasswordKey, "");

  String currentBssid =
      prefs.getString(kBssidKey, "");

  // Verifica se realmente houve alteração.
  bool changed =
      currentSsid != ssid ||
      currentPassword != password ||
      currentBssid != bssid;

  if (!changed) {
    // Não grava novamente os mesmos valores.
    //
    // Isso evita escritas desnecessárias na flash.
    return false;
  }

  // Salva a nova configuração.
  bool ssidOk =
      prefs.putString(kSsidKey, ssid) > 0;

  bool passwordOk =
      prefs.putString(kPasswordKey, password) > 0;

  bool bssidOk =
      prefs.putString(kBssidKey, bssid) > 0;

  if (!ssidOk || !passwordOk || !bssidOk) {
    Serial.println(
        "ERRO: falha ao gravar configuracao de rede na NVS"
    );

    return false;
  }

  Serial.println(
      "NVS: configuracao de rede salva"
  );

  return true;
}

// =========================================================================
// CLEAR
// =========================================================================

void clear() {
  // false = permite escrita.
  //
  // Também cria o namespace caso ele ainda não exista.
  Scope scope(false);

  if (!scope.opened) {
    Serial.println(
        "ERRO: nao foi possivel abrir NVS para limpar"
    );

    return;
  }

  prefs.clear();

  Serial.println(
      "NVS: configuracao de rede removida"
  );
}

// =========================================================================
// PARSE BSSID
// =========================================================================

bool parseBssid(
    const String& text,
    uint8_t out[6]
) {
  // Formato esperado:
  //
  // AA:BB:CC:DD:EE:FF
  //
  // 17 caracteres.
  if (text.length() != 17) {
    return false;
  }

  for (int index = 0; index < 6; index++) {
    int at = index * 3;

    // A partir do segundo byte, verifica o separador.
    if (index > 0) {
      char separator = text[at - 1];

      if (separator != ':' && separator != '-') {
        return false;
      }
    }

    int high = hexValue(text[at]);
    int low = hexValue(text[at + 1]);

    if (high < 0 || low < 0) {
      return false;
    }

    out[index] =
        static_cast<uint8_t>((high << 4) | low);
  }

  return true;
}

}  // namespace network_config