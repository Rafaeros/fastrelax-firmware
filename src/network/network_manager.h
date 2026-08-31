#pragma once

#include <Arduino.h>

// =========================================================================
// GERENCIADOR DE REDE
//
// Fachada sobre o enlace ativo. O resto do firmware pede IP, MAC e estado por
// aqui e não sabe qual meio está em uso.
//
// Com ETHERNET_ENABLED os dois enlaces ficam no ar ao mesmo tempo: o cabo é o
// preferido, o Wi-Fi é reserva. Cabo caiu, o tráfego passa a sair pelo Wi-Fi;
// cabo voltou, o tráfego volta para ele. A troca não reinicia nada e não
// bloqueia — o servidor HTTP e a sessão em andamento não percebem.
//
// O MAC reportado não muda junto com o meio: ele é a identidade da cadeira no
// backend. Ver macAddress().
// =========================================================================

namespace network {

using Event = void (*)();

/**
 * Quem quer reagir à queda e à volta da rede registra antes do begin().
 *
 * A reconexão importa porque o DHCP pode ter entregado outro endereço: o
 * heartbeat seguinte é que avisa o backend para onde mandar o próximo comando.
 * O mesmo vale para a troca de enlace, que também muda o IP sem nunca deixar a
 * cadeira offline — por isso ela dispara onReconnected igual a uma reconexão.
 */
void setEventHandlers(Event onDisconnected, Event onReconnected);

/** Sobe os enlaces configurados em config.h. */
void begin();

/**
 * Reconecta o Wi-Fi com a configuração que acabou de ser gravada.
 *
 * Chamado pelo POST /network depois de a NVS mudar. Sem isto a rede nova só
 * valeria no próximo boot — e a cadeira pode estar numa rede que vai deixar de
 * existir justamente por causa da troca.
 */
void applyConfig();

/** Reconexão, failover e disparo dos eventos. Não bloqueia. */
void loop();

bool isConnected();

/**
 * Identidade da cadeira no backend. Estável: não muda no failover, mesmo que o
 * meio ativo tenha outro endereço de hardware.
 */
String macAddress();

/** IP do enlace que está valendo agora. */
String ipAddress();

/** RSSI do enlace ativo. Zero quando o ativo é o cabo. */
long signalStrength();

const char* linkName();

}  // namespace network
