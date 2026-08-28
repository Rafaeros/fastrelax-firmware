#pragma once

// =========================================================================
// RELÉ
//
// O relé está em paralelo com o botão de liga/desliga da cadeira, não com a
// alimentação dela. Ele não segura a cadeira ligada: ele aperta o botão.
//
// Por isso todo acionamento é um pulso — fecha o contato por RELAY_PULSE_MS e
// abre. Manter fechado durante os cinco ou dez minutos da sessão equivale a
// segurar o botão pressionado o tempo todo: a cadeira entende como botão
// travado, entra em erro, apita e pisca o LED azul.
//
// Ligar e desligar usam o mesmo pulso; quem sabe o que cada um significa é a
// sessão, que guarda se a cadeira já está ligada.
// =========================================================================

namespace relay {

/** Configura o pino e garante o contato aberto. */
void begin();

/** Fecha o contato. `loop()` abre sozinho depois de RELAY_PULSE_MS. */
void pulse();

/** Encerra o pulso no tempo certo. Precisa rodar a cada volta do loop. */
void loop();

/** Verdadeiro enquanto o contato está fechado. */
bool isPulsing();

/**
 * Abre o contato imediatamente, sem esperar o fim do pulso.
 *
 * Rede de segurança: nada no fluxo normal chama isto, mas um contato que ficou
 * fechado é exatamente o defeito que o pulso existe para evitar.
 */
void release();

}  // namespace relay
