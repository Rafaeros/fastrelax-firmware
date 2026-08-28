#pragma once

#include <Arduino.h>

// =========================================================================
// GERENCIADOR DE REDE
//
// Fachada sobre o enlace ativo (Wi-Fi ou Ethernet). O resto do firmware pede
// IP, MAC e estado por aqui e não sabe qual meio está em uso.
// =========================================================================

namespace network {

using Event = void (*)();

/**
 * Quem quer reagir à queda e à volta da rede registra antes do begin().
 *
 * A reconexão importa porque o DHCP pode ter entregado outro endereço: o
 * heartbeat seguinte é que avisa o backend para onde mandar o próximo comando.
 */
void setEventHandlers(Event onDisconnected, Event onReconnected);

/** Sobe o enlace escolhido em config.h. */
void begin();

/**
 * Reconecta com a configuração que acabou de ser gravada.
 *
 * Chamado pelo POST /network depois de a NVS mudar. Sem isto a rede nova só
 * valeria no próximo boot — e a cadeira pode estar numa rede que vai deixar de
 * existir justamente por causa da troca.
 */
void applyConfig();

/** Reconexão e disparo dos eventos. Não bloqueia. */
void loop();

bool isConnected();
String macAddress();
String ipAddress();
long signalStrength();
const char* linkName();

}  // namespace network
