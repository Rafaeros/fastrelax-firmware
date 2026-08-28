#pragma once

#include <Arduino.h>

// =========================================================================
// PÁGINA LOCAL
//
// Diagnóstico apenas: quem agenda e inicia a sessão é o aplicativo. O botão
// aqui é parada de emergência, para interromper na marra se necessário.
//
// Fica isolada do servidor porque é a parte que mais muda (marca, texto,
// layout) e a que menos tem a ver com o controle da cadeira.
// =========================================================================

namespace portal {

/** HTML completo, com o token embutido para o botão de parada. */
String pageHtml();

}  // namespace portal
