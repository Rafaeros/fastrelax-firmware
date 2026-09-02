#pragma once

// =========================================================================
// RELÉ DE CORTE DE ENERGIA DO PAINEL
//
// Segundo relé, em série com a alimentação do painel (não em paralelo com o
// botão, como src/relay/relay.h). Cabeado NC: contato fechado = painel
// energizado.
//
// Diferente do relé de sessão, este nunca pulsa — o nível fica sustentado
// até a próxima mudança de estado, porque é ele quem decide se o painel tem
// energia ou não a qualquer instante, não um toque de botão.
// =========================================================================

namespace power_relay {

/** Configura o pino e aplica o estado já conhecido de chair_state — antes de qualquer rede. */
void begin();

/**
 * Aplica o estado de ativação no relé.
 *
 * `active = true`  -> desenergiza (POWER_RELAY_OFF) -> NC fechado -> painel ligado.
 * `active = false` -> energiza (POWER_RELAY_ON) -> abre o NC -> painel cortado.
 */
void apply(bool active);

}  // namespace power_relay
