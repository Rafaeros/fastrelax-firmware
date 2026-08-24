#pragma once

// =========================================================================
// CONFIGURAÇÃO DA CADEIRA
//
// Ajuste os valores conforme a instalação. Mantenha este arquivo fora de
// repositório público: contém a senha do Wi-Fi e o token do dispositivo.
// =========================================================================

// ---- Rede ----
#define WIFI_SSID       "SSID"
#define WIFI_PASSWORD   "PASSWORD"

// Nome que aparece no DHCP do roteador — facilita localizar a cadeira na rede.
#define DEVICE_HOSTNAME "fastrelax-chair-01"

// ---- Backend ----
// Endereço do fastrelax-api. Precisa ser alcançável a partir da rede em que o
// ESP32 se conecta.
#define BACKEND_HOST    "IP"
#define BACKEND_PORT    8090
#define BACKEND_PATH    "/api/v1"

// Mesmo valor de CHAIR_DEVICE_TOKEN no .env do backend. Usado nos dois
// sentidos: o ESP32 envia no heartbeat e confere nos comandos que recebe.
#define DEVICE_TOKEN    "TOKEN"

// ---- Hardware ----
#define RELAY_PIN       26

// Nível lógico do módulo relé. Se acionar invertido, troque os dois valores.
#define RELAY_ON        LOW
#define RELAY_OFF       HIGH

// ---- Tempos ----
// A porta HTTP não é configurável: o ESP32 escuta fixo na 80, que é o valor
// que o backend assume ao montar a URL do comando. Mudar aqui exigiria
// informar a porta no cadastro da cadeira.

// Intervalo entre heartbeats. O backend marca a cadeira como offline após
// app.chair.offline-after-seconds (padrão 180s); mantenha ao menos três
// batidas dentro dessa janela para tolerar uma perda ocasional
#define HEARTBEAT_INTERVAL_MS   60000UL

// Espera entre tentativas de reconexão do Wi-Fi.
#define WIFI_RETRY_MS           10000UL

// Teto absoluto de acionamento. Se chegar um comando com duração excessiva, ou
// algo travar, o relé desliga assim mesmo.
#define MAX_SESSION_MS          (30UL * 60UL * 1000UL)

// ---- Teste de relé ----
// Duração padrão do acionamento manual, usado para conferir a fiação.
#define RELAY_TEST_SECONDS      10
// Teto do teste: mesmo pedindo mais, o relé não fica ligado além disto.
#define RELAY_TEST_MAX_SECONDS  60
