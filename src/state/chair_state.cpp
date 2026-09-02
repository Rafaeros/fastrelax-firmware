#include "state/chair_state.h"

#include <Preferences.h>

namespace chair_state {
namespace {

constexpr const char* kNamespace = "fr-state";
constexpr const char* kActiveKey = "active";

Preferences prefs;
bool cachedActive = true;

}  // namespace

void begin() {
  bool opened = prefs.begin(kNamespace, false);
  if (!opened) {
    Serial.println("ERRO: nao foi possivel abrir NVS 'fr-state'; assumindo ativa");
    cachedActive = true;
    return;
  }

  // Ausente = cadeira nunca recebeu um estado explícito; o padrão seguro é
  // ativada, não cortar energia de uma instalação ainda não configurada.
  cachedActive = prefs.getBool(kActiveKey, true);
  prefs.end();
}

bool isActive() {
  return cachedActive;
}

bool setActive(bool active) {
  if (active == cachedActive) {
    return false;
  }

  bool opened = prefs.begin(kNamespace, false);
  if (!opened) {
    Serial.println("ERRO: nao foi possivel abrir NVS 'fr-state' para gravar");
    return false;
  }

  prefs.putBool(kActiveKey, active);
  prefs.end();

  cachedActive = active;
  return true;
}

}  // namespace chair_state
