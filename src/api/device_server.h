#pragma once

// =========================================================================
// API DO DISPOSITIVO
//
// Rotas que o backend chama:
//   POST /start        liga o relé pela duração informada
//   POST /stop         desliga antes do previsto
//   POST /relay-test   acionamento manual, para conferir a fiação
//   POST /network      grava SSID, senha e BSSID da empresa na NVS
//   POST /power        liga/desliga o relé de corte de energia do painel
//   GET  /status       estado atual, para diagnóstico e para a página local
//   GET  /             página local
//
// Porta fixa 80: o backend monta a URL com esse valor, então não há o que
// configurar nem o que informar no heartbeat.
// =========================================================================

namespace device_api {

void begin();

/** Atende as requisições pendentes. Precisa rodar a cada volta do loop. */
void loop();

}  // namespace device_api
