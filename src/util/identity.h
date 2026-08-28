#pragma once

// =========================================================================
// IDENTIFICAÇÃO
//
// Banner com os dados necessários para cadastrar a cadeira no painel. Vive
// separado porque é impresso de três lugares: no boot, na reconexão e quando o
// backend responde que o MAC é desconhecido.
// =========================================================================

namespace identity {

/**
 * O MAC é a identidade do dispositivo no backend e não muda; o IP é só o
 * endereço atual e o heartbeat o mantém atualizado sozinho.
 */
void print(const char* header);

}  // namespace identity
