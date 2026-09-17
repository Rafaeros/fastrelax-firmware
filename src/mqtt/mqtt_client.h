#pragma once

#include <Arduino.h>

// =========================================================================
// MQTT — PREPARADO, AINDA NÃO USADO
//
// Hoje o caminho é HTTP nos dois sentidos: o ESP32 bate heartbeat no backend e
// o backend abre conexão para mandar /start e /stop. Isso exige que a cadeira
// seja alcançável a partir do servidor — IP estável, porta 80 aberta, mesma
// rede. MQTT inverte a direção: a cadeira abre a conexão e a mantém, então o
// comando chega mesmo atrás de NAT ou em outra VLAN.
//
// Ligue MQTT_ENABLED em config.h para ativar. Enquanto estiver desligado, tudo
// aqui vira no-op e o HTTP continua sendo o único transporte.
//
// Broker: funciona igual com HiveMQ (Cloud ou self-hosted) e com o RabbitMQ da
// casa com o plugin rabbitmq_mqtt habilitado — a diferença fica em config.h.
// Detalhes de cada um no cabeçalho de mqtt_client.cpp.
// =========================================================================

namespace mqtt {

/** Prepara o cliente. Não bloqueia: a conexão acontece no loop(). */
void begin();

/** Reconexão, keep-alive e publicação periódica de estado. */
void loop();

bool isConnected();

/**
 * Derruba a conexão atual para forçar reconexão imediata com a configuração
 * mais recente da NVS (ver mqtt_config.h).
 *
 * Espelha network::applyConfig(): quem grava um host/usuário/senha novo via
 * POST /mqtt precisa chamar isto depois, senão o cliente só percebe a
 * mudança na próxima falha de keep-alive.
 */
void applyConfig();

/**
 * Publica o estado atual da sessão em `<prefixo>/<mac>/status`, retido.
 * Quem assinar depois recebe o último estado sem esperar o próximo ciclo.
 */
bool publishStatus();

/** Publicação avulsa num subtópico da cadeira. */
bool publish(const char* subtopic, const String& payload, bool retained);

}  // namespace mqtt
