#pragma once

#include <Arduino.h>

// =========================================================================
// SESSÃO
//
// Máquina de estados do acionamento. A contagem do tempo é local e deliberada:
// se a rede cair depois do /start, a cadeira precisa desligar sozinha. O
// backend também conta, mas apenas para reconciliar o estado da sessão no
// banco — quem garante o desligamento físico é este módulo.
//
// O relé aperta o botão da cadeira em vez de segurar a alimentação (ver
// src/relay/relay.h), então todo acionamento é um pulso: um para ligar, no
// começo, e outro para desligar, no fim. Entre eles o contato fica aberto e
// quem está ligada é a cadeira, não o relé — por isso este módulo guarda se a
// cadeira está ligada: é a única forma de saber se o próximo pulso liga ou
// desliga.
//
// Depois do pulso que desliga vem a estabilização: a cadeira leva
// CHAIR_COOLDOWN_MS recolhendo os mecanismos, e apertar o botão nesse meio
// tempo trava o aparelho. Durante a janela a fase é COOLDOWN e todo comando de
// acionamento é recusado.
//
// Ciclo completo:
//   IDLE -> STARTING -> RUNNING -> COOLDOWN -> IDLE
//            (espera)   (pulso     (pulso
//                        de ligar)  de desligar)
// =========================================================================

namespace session {

// TESTING é o acionamento manual do relé: comporta-se como RUNNING, mas fica
// separado para o log e o /status deixarem claro que não há sessão real.
enum class Phase { IDLE, STARTING, RUNNING, TESTING, COOLDOWN };

/** Resposta a um pedido de acionamento, traduzida em status HTTP pela API. */
enum class StartResult { ACCEPTED, BUSY, COOLING_DOWN, INVALID_DURATION };

/** Leitura consistente do estado, para o /status e a página local. */
struct Snapshot {
  Phase phase;
  const char* phaseName;
  /**
   * Segundos que faltam para o fim da fase atual: da espera em STARTING, da
   * massagem em RUNNING, da estabilização em COOLDOWN.
   */
  unsigned long remainingSeconds;
  long sessionId;
  /**
   * Se a cadeira está ligada — não se o relé está fechado.
   *
   * O contato fica fechado por RELAY_PULSE_MS duas vezes na sessão inteira; o
   * que interessa a quem lê o status é a cadeira, não o pulso.
   */
  bool chairOn;
};

void begin();

/**
 * Agenda a sessão.
 *
 * Recusa com BUSY quando já existe uma em andamento: duas pessoas não usam a
 * mesma cadeira ao mesmo tempo, e sobrescrever em silêncio esconderia o
 * problema de quem já estava sentado. Recusa com COOLING_DOWN enquanto a
 * cadeira estabiliza.
 */
StartResult start(long sessionId, long durationSeconds, long startDelaySeconds);

/**
 * Liga e desliga a cadeira depois de alguns segundos, sem sessão — serve para
 * conferir a fiação na instalação. Sujeito às mesmas recusas do start.
 */
StartResult startRelayTest(long durationSeconds);

/**
 * Encerra antes do previsto e abre a janela de estabilização. Chamar com a
 * cadeira parada não é erro.
 *
 * Só pulsa se a cadeira estiver ligada: parar durante a espera do início
 * (fase STARTING) com um pulso ligaria a cadeira em vez de desligá-la — e aí
 * não há o que estabilizar, a fase volta direto para IDLE.
 */
void stop(const char* reason);

/** Avança a máquina de estados; precisa rodar a cada volta do loop. */
void loop();

/** Duração acordada para o acionamento em curso, já com os tetos aplicados. */
unsigned long plannedDurationSeconds();

Snapshot snapshot();

/** Pronta para uma sessão nova: nem em uso, nem estabilizando. */
bool isIdle();

}  // namespace session
