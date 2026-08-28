#pragma once

// =========================================================================
// BACKEND
//
// Único ponto que sai do dispositivo em direção ao fastrelax-api.
// =========================================================================

namespace backend {

/** Primeira batida no boot, para o painel mostrar a cadeira sem esperar o intervalo. */
void begin();

/** Heartbeat periódico. Precisa rodar a cada volta do loop. */
void loop();

/**
 * Avisa o backend que está vivo e informa o endereço atual.
 *
 * O IP vai em toda batida porque o DHCP pode trocá-lo sem aviso — é assim que
 * o backend sabe para onde mandar o próximo comando. A porta não é enviada: o
 * backend assume 80, que é onde este firmware escuta.
 */
void sendHeartbeat();

/** Reimprime o banner de cadastro na próxima recusa por MAC desconhecido. */
void forgetRegistration();

}  // namespace backend
