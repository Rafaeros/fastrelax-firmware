#pragma once

// =========================================================================
// ESTADO DE ATIVAÇÃO DA CADEIRA
//
// Guarda na NVS se esta cadeira está ativada pela Physical — o mesmo campo
// `active` do cadastro no backend. Existe para o relé de corte de energia
// (ver src/power/power_relay.h) saber que nível aplicar já no boot, antes de
// qualquer tentativa de rede: uma queda de energia não pode fazer o painel
// voltar ligado numa cadeira que estava desativada.
//
// Uma cadeira nova, sem nada gravado ainda, começa ativada — é o estado
// seguro por padrão: silêncio da NVS não deve equivaler a "desativada" e
// cortar energia de uma instalação que ainda nem foi configurada.
// =========================================================================

namespace chair_state {

/** Lê o estado persistido (ou o padrão `true`, se nunca foi gravado). */
void begin();

/** Estado atualmente conhecido, já carregado por begin(). */
bool isActive();

/**
 * Grava o novo estado na NVS.
 *
 * Retorna true somente quando o valor realmente mudou — evita escrita
 * desnecessária na flash quando o heartbeat só está confirmando o que já
 * estava certo.
 */
bool setActive(bool active);

}  // namespace chair_state
