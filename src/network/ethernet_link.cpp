#include "network/ethernet_link.h"

#include "config.h"

#if ETHERNET_ENABLED
#include <WiFi.h>  // WiFiEvent_t / eventos do stack de rede, compartilhados com o ETH
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_eth.h>
#include <esp_eth_netif_glue.h>
#include <esp_mac.h>
#include <esp_netif.h>
#endif

const char* EthernetLink::name() const {
  return "ethernet";
}

#if ETHERNET_ENABLED

// Ganchos internos do Arduino, os mesmos que a classe ETH da placa usa. O
// primeiro sobe esp_netif e a ponte de eventos; o segundo registra a interface
// no WiFiGeneric, para que WiFi.onEvent e WiFi.localIP enxerguem o cabo.
extern void tcpipInit();
extern void add_esp_interface_netif(esp_interface_t interface, esp_netif_t* esp_netif);

namespace {

// O enlace sobe por eventos, não por polling: guardar o estado aqui evita
// consultar o driver antes de ele existir.
volatile bool ethConnected = false;

esp_eth_handle_t ethHandle = nullptr;
esp_netif_t* ethNetif = nullptr;

void onNetworkEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      esp_netif_set_hostname(ethNetif, DEVICE_HOSTNAME);
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ethConnected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      ethConnected = false;
      break;
    default:
      break;
  }
}

/**
 * Sobe o W5500 e o pendura no stack de rede.
 *
 * O módulo é externo e falado por SPI, então nada disso vem pronto: é preciso
 * abrir o barramento, criar MAC e PHY, instalar o driver e só então entregar a
 * interface ao lwIP. Depois desta função o WebServer e o HTTPClient funcionam
 * sem saber que o meio mudou.
 */
bool startW5500() {
  tcpipInit();

  // O driver do W5500 registra o handler do pino INT, mas não instala o serviço
  // de ISR do GPIO — nos exemplos do ESP-IDF quem instala é a aplicação. Sob
  // Arduino ele só existe se algo já tiver chamado attachInterrupt, e este
  // firmware não chama. Sem isto o esp_eth sobe sem erro fatal, mas o W5500
  // nunca avisa que chegou pacote: o DHCP não fecha e o cabo parece morto.
  esp_err_t isrErr = gpio_install_isr_service(0);
  if (isrErr != ESP_OK && isrErr != ESP_ERR_INVALID_STATE) {
    Serial.printf("ethernet: gpio_install_isr_service falhou (%d)\n", isrErr);
    return false;
  }

  // O Wi-Fi fica de reserva quente, então os dois enlaces podem estar no ar ao
  // mesmo tempo. Quem escolhe por onde o tráfego sai é a prioridade de rota do
  // lwIP, e o padrão do Ethernet (50) perde para o do Wi-Fi (100) — sem subir
  // isto o cabo estaria ligado e ocioso. Acima de 100 o cabo é o caminho
  // preferido enquanto existir, e a volta para o Wi-Fi é automática quando ele
  // some. static porque o esp_netif guarda o ponteiro.
  static esp_netif_inherent_config_t netifBase = *ESP_NETIF_BASE_DEFAULT_ETH;
  netifBase.route_prio = 128;

  esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH();
  netifConfig.base = &netifBase;
  ethNetif = esp_netif_new(&netifConfig);
  if (ethNetif == nullptr) {
    Serial.println("ethernet: esp_netif_new falhou");
    return false;
  }

  spi_bus_config_t busConfig = {};
  busConfig.miso_io_num = ETH_SPI_MISO_PIN;
  busConfig.mosi_io_num = ETH_SPI_MOSI_PIN;
  busConfig.sclk_io_num = ETH_SPI_SCK_PIN;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;

  esp_err_t err = spi_bus_initialize(ETH_SPI_HOST, &busConfig, SPI_DMA_CH_AUTO);
  // ESP_ERR_INVALID_STATE significa que o barramento já estava aberto por
  // outro periférico; isso não é erro, só não somos nós que o abrimos.
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("ethernet: spi_bus_initialize falhou (%d)\n", err);
    return false;
  }

  // command_bits/address_bits são o formato de quadro do W5500: 16 bits de
  // endereço e 8 de controle. Errar isto deixa o chip mudo, sem erro visível.
  spi_device_interface_config_t deviceConfig = {};
  deviceConfig.command_bits = 16;
  deviceConfig.address_bits = 8;
  deviceConfig.mode = 0;
  deviceConfig.clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000;
  deviceConfig.spics_io_num = ETH_SPI_CS_PIN;
  deviceConfig.queue_size = 20;

  spi_device_handle_t spiHandle = nullptr;
  if (spi_bus_add_device(ETH_SPI_HOST, &deviceConfig, &spiHandle) != ESP_OK) {
    Serial.println("ethernet: spi_bus_add_device falhou");
    return false;
  }

  eth_w5500_config_t w5500Config = ETH_W5500_DEFAULT_CONFIG(spiHandle);
  w5500Config.int_gpio_num = ETH_SPI_INT_PIN;

  eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
  macConfig.sw_reset_timeout_ms = 1000;

  esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500Config, &macConfig);
  if (mac == nullptr) {
    Serial.println("ethernet: esp_eth_mac_new_w5500 falhou");
    return false;
  }

  eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
  phyConfig.phy_addr = ETH_PHY_ADDRESS;
  phyConfig.reset_gpio_num = ETH_SPI_RST_PIN;

  esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phyConfig);
  if (phy == nullptr) {
    Serial.println("ethernet: esp_eth_phy_new_w5500 falhou");
    return false;
  }

  esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac, phy);
  if (esp_eth_driver_install(&ethConfig, &ethHandle) != ESP_OK || ethHandle == nullptr) {
    Serial.println("ethernet: esp_eth_driver_install falhou");
    return false;
  }

  // O W5500 não tem MAC de fábrica: sem isto ele sobe com endereço zerado e o
  // DHCP não responde. Usamos o MAC de Ethernet derivado do eFuse do ESP32, que
  // é estável entre boots — o backend identifica a cadeira por ele.
  uint8_t macAddr[6] = {0};
  if (esp_read_mac(macAddr, ESP_MAC_ETH) == ESP_OK) {
    esp_eth_ioctl(ethHandle, ETH_CMD_S_MAC_ADDR, macAddr);
  }

  if (esp_netif_attach(ethNetif, esp_eth_new_netif_glue(ethHandle)) != ESP_OK) {
    Serial.println("ethernet: esp_netif_attach falhou");
    return false;
  }

  // Sem isto os eventos ARDUINO_EVENT_ETH_* não chegam ao WiFi.onEvent.
  add_esp_interface_netif(ESP_IF_ETH, ethNetif);

  if (esp_eth_start(ethHandle) != ESP_OK) {
    Serial.println("ethernet: esp_eth_start falhou");
    return false;
  }

  return true;
}

}  // namespace

void EthernetLink::begin() {
  Serial.println("subindo enlace ethernet (W5500 por SPI)...");
  WiFi.onEvent(onNetworkEvent);

  if (!startW5500()) {
    linkUp_ = false;
    Serial.println("ethernet: modulo W5500 nao inicializou; confira a fiacao SPI e o pino INT");
    return;
  }

  unsigned long deadline = millis() + ETH_CONNECT_TIMEOUT_MS;
  while (!ethConnected && millis() < deadline) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  linkUp_ = ethConnected;
  if (!linkUp_) {
    Serial.println("ethernet sem enlace; confira o cabo e a alimentacao do modulo");
    Serial.printf("MAC address: %s\n", macAddress().c_str());
  }
}

void EthernetLink::loop() {
  // O driver reconecta sozinho pelos eventos; aqui só refletimos o estado.
  linkUp_ = ethConnected;
}

bool EthernetLink::isConnected() const {
  return linkUp_;
}

String EthernetLink::macAddress() const {
  // Lido do eFuse, não do driver: vale mesmo quando o W5500 não subiu, que é
  // justamente quando o log precisa mostrar a identidade da placa.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_ETH);

  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  return String(buffer);
}

String EthernetLink::ipAddress() const {
  esp_netif_ip_info_t info = {};
  if (ethNetif == nullptr || esp_netif_get_ip_info(ethNetif, &info) != ESP_OK) {
    return String("0.0.0.0");
  }
  return IPAddress(info.ip.addr).toString();
}

#else  // ETHERNET_ENABLED

void EthernetLink::begin() {
  Serial.println("ethernet desabilitada (ETHERNET_ENABLED = 0 em config.h)");
}

void EthernetLink::loop() {}

bool EthernetLink::isConnected() const {
  return false;
}

String EthernetLink::macAddress() const {
  return String("00:00:00:00:00:00");
}

String EthernetLink::ipAddress() const {
  return String("0.0.0.0");
}

#endif  // ETHERNET_ENABLED

long EthernetLink::signalStrength() const {
  return 0;
}
