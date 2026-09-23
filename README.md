# FastRelax Firmware

Firmware ESP32 (PlatformIO/Arduino) que controla a cadeira de massagem: aciona o relé da sessão, corta a energia do painel quando a cadeira é desativada, e fala com o [fastrelax-api](../fastrelax-api) por HTTP (heartbeat + comandos) e opcionalmente por MQTT.

## Requisitos

- [PlatformIO](https://platformio.org/) (CLI ou extensão VS Code)
- Placa ESP32 DevKit
- Módulo Ethernet W5500 (opcional, ligado por SPI) — ver `ETHERNET_ENABLED` em `config.h`

## Configuração

Credenciais e endereços de rede não são versionados — cada cadeira/bancada mantém sua própria cópia local:

```bash
cp include/config.h.example include/config.h
```

Edite `include/config.h` com:
- SSID e senha do Wi-Fi de primeiro boot (usado só até o backend enviar `POST /network` com a configuração definitiva, que passa a viver na NVS)
- Endereço do `fastrelax-api` (`BACKEND_HOST`/`BACKEND_PORT`)
- Credenciais do broker MQTT, se `MQTT_ENABLED` estiver ligado

`config.h` nunca deve ser commitado — está no `.gitignore`. Se precisar trocar a senha do Wi-Fi ou do broker em produção, isso é feito por `POST /network` e `POST /mqtt` (ver `src/api/device_server.cpp`), não editando este arquivo numa cadeira já em campo.

## Build e upload

```bash
pio run                # compila
pio run --target upload  # grava na placa
pio device monitor       # monitor serial (115200 baud)
```

O binário compilado também é copiado para `meus_firmwares/firmware.bin` após o build (ver `copy_bin.py`), usado no catálogo de firmwares do backend.

## Estrutura

```
src/
├── api/        # servidor HTTP local da cadeira (comandos do backend, página de status)
├── backend/    # cliente HTTP que fala com o fastrelax-api (heartbeat)
├── mqtt/       # transporte alternativo ao HTTP (comandos via broker)
├── network/    # Wi-Fi, Ethernet (fallback) e persistência de config de rede na NVS
├── power/      # relé de corte de energia do painel
├── relay/      # relé de acionamento da sessão (pulso, como apertar o botão físico)
├── session/    # estado da sessão em andamento
├── state/      # estado persistente da cadeira (ativa/desativada) na NVS
└── util/       # identidade do dispositivo (token pareado com o backend), parsing JSON
```

## Comunicação com o backend

- **Heartbeat** (`POST /chairs/heartbeat`, HTTP, cadeira → backend): a cadeira se identifica pelo MAC e pelo token gerado no primeiro boot. O backend pareia com esse token no primeiro heartbeat.
- **Comandos** (`X-Device-Token` no cabeçalho, backend → cadeira, ou via tópicos MQTT `fastrelax/chairs/<mac>/cmd/*`): start, stop, relay-test, network, power.
- **Estabilização entre sessões**: a cadeira recusa `/start` e `/relay-test` com 409 enquanto `CHAIR_COOLDOWN_MS` não tiver passado desde o último desligamento — é a última barreira, além da folga que o backend já aplica no agendamento.

Detalhes de cada rota/tópico estão comentados em `src/api/device_server.h` e `src/mqtt/mqtt_config.h`.
