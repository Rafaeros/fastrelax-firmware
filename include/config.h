#pragma once

// =========================================================================
// CONFIGURAÇÃO DA CADEIRA
//
// Ajuste os valores conforme a instalação. Mantenha este arquivo fora de
// repositório público: contém a senha do Wi-Fi e o token do dispositivo.
// =========================================================================

// ---- Rede ----
// Rede de primeiro boot, e só isso. A partir do momento em que o backend envia
// POST /network, SSID, senha e BSSID passam a viver na NVS e estes valores não
// são mais consultados — ver src/network/network_config.h.
//
// Uma placa recém-gravada precisa entrar em *alguma* rede para conseguir
// receber a configuração de verdade; é para isso que eles existem. Aponte para
// a rede da bancada, não para a do cliente.
#define WIFI_SSID       "LANX CABLES"
#define WIFI_PASSWORD   "Lanx@2029"

// Nome que aparece no DHCP do roteador — facilita localizar a cadeira na rede.
#define DEVICE_HOSTNAME "fastrelax-chair-01"

// ---- Backend ----
// Endereço do fastrelax-api. Precisa ser alcançável a partir da rede em que o
// ESP32 se conecta.
#define BACKEND_HOST    "10.48.0.189"
#define BACKEND_PORT    8090
#define BACKEND_PATH    "/api/v1"

// Mesmo valor de CHAIR_DEVICE_TOKEN no .env do backend. Usado nos dois
// sentidos: o ESP32 envia no heartbeat e confere nos comandos que recebe.
#define DEVICE_TOKEN    "123d5f84b77a959d33971c7aa39841cdebead7beb4109f2cfc25c3c5ea8a7740"

// ---- Hardware ----
// O relé fica em paralelo com o botão de liga/desliga da cadeira: fechar o
// contato é o mesmo que apertar o botão. Ele nunca fica fechado durante a
// sessão — a cadeira leria como botão travado, entraria em erro, apitaria e
// piscaria o LED azul. Ver src/relay/relay.h.
#define RELAY_PIN       26

// Nível lógico que fecha o contato. Se acionar invertido, troque os dois valores.
#define RELAY_ON        LOW
#define RELAY_OFF       HIGH

// Duração do pulso, ou seja: por quanto tempo o botão fica "apertado". Curto
// demais a cadeira ignora; longo demais ela entende como pressionar e segurar,
// que em muitos aparelhos é outra função. 300ms é o toque humano típico.
#define RELAY_PULSE_MS  300UL

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

// Quanto o boot espera pela primeira conexão antes de seguir em frente. Não
// conectar não impede o firmware de subir: o relé e a página local funcionam,
// e a reconexão continua em background.
#define WIFI_CONNECT_TIMEOUT_MS 20000UL

// Teto absoluto de acionamento. Se chegar um comando com duração excessiva, ou
// algo travar, o relé desliga assim mesmo.
#define MAX_SESSION_MS          (30UL * 60UL * 1000UL)

// ---- Teste de relé ----
// Duração padrão do acionamento manual, usado para conferir a fiação.
#define RELAY_TEST_SECONDS      10
// Teto do teste: mesmo pedindo mais, o relé não fica ligado além disto.
#define RELAY_TEST_MAX_SECONDS  60

// ---- Ethernet ----
// Ligue ETHERNET_ENABLED em 1 quando a cadeira ganhar um ponto de rede com
// cabo. O cabo passa a ser o caminho preferido e o Wi-Fi continua no ar como
// reserva: se o cabo cair, o tráfego sai pelo Wi-Fi sem a cadeira ficar
// offline; quando o cabo volta, volta a ser ele. Nada mais no firmware muda,
// porque heartbeat, servidor HTTP e página local falam com a fachada de rede.
//
// Deixe as credenciais de Wi-Fi válidas mesmo usando cabo — é delas que a
// reserva depende.
//
// O ESP32 desta placa é um DevKit comum, sem PHY RMII embarcado. O enlace
// cabeado vem de um módulo W5500 externo, ligado por SPI. Confira os pinos
// abaixo contra a fiação real antes de ligar.
#define ETHERNET_ENABLED        1

#if ETHERNET_ENABLED

// Barramento SPI que fala com o W5500. SPI3_HOST é o VSPI do ESP32; troque
// para SPI2_HOST (HSPI) se o VSPI já estiver ocupado por outro periférico.
#define ETH_SPI_HOST            SPI3_HOST

// Pinos do barramento — estes são os defaults do VSPI.
#define ETH_SPI_SCK_PIN         18
#define ETH_SPI_MISO_PIN        19
#define ETH_SPI_MOSI_PIN        23
#define ETH_SPI_CS_PIN          5

// O driver W5500 do ESP-IDF 4.4 é orientado a interrupção: não existe modo
// polling. Este pino é obrigatório e precisa estar ligado ao INT do módulo.
#define ETH_SPI_INT_PIN         4

// Reset do módulo. -1 quando o RST não é controlado pelo ESP32.
#define ETH_SPI_RST_PIN         14

// Clock do SPI. O W5500 aguenta bem mais, mas fio comprido ou jumper solto
// derruba o enlace antes de dar erro claro; 20 MHz é o ponto seguro.
#define ETH_SPI_CLOCK_MHZ       20

// Endereço do PHY interno do W5500. Sempre 1.
#define ETH_PHY_ADDRESS         1

// Quanto o boot espera pelo IP do cabo antes de decidir se o Wi-Fi entra como
// enlace principal ou só como reserva. Cabo costuma subir mais rápido que o
// Wi-Fi, então esperar aqui sai barato.
#define ETH_CONNECT_TIMEOUT_MS  10000UL
#endif

// ---- MQTT (preparado, ainda não usado) ----
// Transporte alternativo ao HTTP. Hoje o backend precisa alcançar a cadeira
// para mandar /start; com MQTT é a cadeira que abre e mantém a conexão, então
// o comando chega mesmo atrás de NAT ou em outra VLAN. Ligue MQTT_ENABLED em 1
// para ativar — o HTTP continua funcionando em paralelo.
#define MQTT_ENABLED            0

#if MQTT_ENABLED
// Endereço do broker.
//   HiveMQ Cloud  -> "xxxxx.s1.eu.hivemq.cloud", porta 8883, MQTT_USE_TLS 1
//   RabbitMQ      -> IP do servidor, porta 1883 (8883 com TLS), depois de
//                    `rabbitmq-plugins enable rabbitmq_mqtt`
#define MQTT_HOST               "10.48.0.189"
#define MQTT_PORT               1883

// TLS é obrigatório no HiveMQ Cloud e opcional no RabbitMQ da rede interna.
#define MQTT_USE_TLS            0

// Credenciais do broker. No RabbitMQ são as mesmas do AMQP; no HiveMQ saem do
// painel de Access Management.
#define MQTT_USERNAME           "fastrelax-chair"
#define MQTT_PASSWORD           "TROQUE"

// Certificado raiz do broker, em PEM. Sem ele e com TLS ligado o firmware
// conecta sem validar — bom para um teste, ruim para deixar assim: sem
// validação, um intermediário na rede pode se passar pelo broker.
// #define MQTT_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"

// Raiz dos tópicos. A cadeira acrescenta o próprio MAC:
//   <prefixo>/<mac>/status      estado da sessão, retido (cadeira -> backend)
//   <prefixo>/<mac>/online      "online"/"offline", retido, com Last Will
//   <prefixo>/<mac>/ack         desfecho do último comando
//   <prefixo>/<mac>/cmd/start   {"token":"...","sessionId":1,"durationSeconds":300}
//   <prefixo>/<mac>/cmd/stop    {"token":"..."}
//   <prefixo>/<mac>/cmd/relay-test  {"token":"...","durationSeconds":10}
// No RabbitMQ estes viram routing keys no exchange amq.topic com '.' no lugar
// de '/' — o backend consome por AMQP normal, sem falar MQTT.
#define MQTT_TOPIC_PREFIX       "fastrelax/chairs"

// QoS da assinatura dos comandos. O plugin do RabbitMQ vai até 1, e o HiveMQ
// gratuito também — 2 não é opção em nenhum dos dois.
#define MQTT_QOS                1

// Silêncio tolerado antes de o broker considerar a cadeira morta e publicar o
// Last Will. Mais curto que a janela do heartbeat HTTP de propósito: é essa
// diferença que justifica o MQTT.
#define MQTT_KEEPALIVE_SECONDS  30

// Espera entre tentativas de reconexão com o broker.
#define MQTT_RETRY_MS           5000UL

// Frequência da publicação de estado.
#define MQTT_STATUS_INTERVAL_MS 15000UL
#endif

// ---- Estabilização entre sessões ----
// Depois do pulso que desliga, a cadeira precisa de um tempo parada antes de
// aceitar o próximo acionamento: ela recolhe os mecanismos e volta à posição
// inicial. Apertar o botão no meio disso é o que trava o aparelho.
//
// Enquanto a janela corre, o firmware recusa /start e /relay-test com 409 e
// informa quantos segundos faltam. Esta é a última barreira — o backend não
// sabe deste tempo ao montar a agenda, e as sessões nascem coladas uma na
// outra.
#define CHAIR_COOLDOWN_MS       (40UL * 1000UL)
