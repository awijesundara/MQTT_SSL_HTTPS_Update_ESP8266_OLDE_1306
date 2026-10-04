# ESP8266 MQTT/SSL Sensor Node with OLED Status Display and HTTPS OTA Update

An ESP8266 sketch that reads temperature and humidity from a DHT22 sensor,
publishes the readings to an MQTT broker over TLS, shows live Wi-Fi/MQTT/
firmware status on an SSD1306 OLED, and checks a remote server for newer
firmware to install over-the-air.

[![CI](https://github.com/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306/actions/workflows/ci.yml/badge.svg)](https://github.com/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306/actions/workflows/ci.yml)
[![Last commit](https://img.shields.io/github/last-commit/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306/master)](https://github.com/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306/commits/master)
[![Top language](https://img.shields.io/github/languages/top/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306)](https://github.com/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306)
[![Code size](https://img.shields.io/github/languages/code-size/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306)](https://github.com/awijesundara/MQTT_SSL_HTTPS_Update_ESP8266_OLDE_1306)
[![ESP8266](https://img.shields.io/badge/ESP8266-NodeMCU-E7352C?logo=espressif&logoColor=white)](platformio.ini)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-F5822A?logo=platformio&logoColor=white)](platformio.ini)
[![MQTT](https://img.shields.io/badge/MQTT-TLS-660066?logo=mqtt&logoColor=white)](IoT_ESP8266_MCU_OLED.ino)
[![OTA](https://img.shields.io/badge/OTA-HTTPS-2E7D32)](IoT_ESP8266_MCU_OLED.ino)

## What it does

1. Connects to Wi-Fi and syncs the clock over SNTP.
2. Connects to an MQTT broker on port 8883 using `WiFiClientSecure`
   (BearSSL) with a pinned root CA certificate, then subscribes to two
   topics used to trigger firmware-update checks.
3. Every 5 seconds, publishes a heartbeat, the running firmware version,
   and DHT22 temperature (C/F) and humidity readings to MQTT topics
   scoped under `HOSTNAME`.
4. On boot, and whenever an MQTT message announces a newer firmware
   version, calls an HTTP endpoint (`<FW_BASE_URL><MAC>.version`) to
   check the latest available version. If newer than the running
   firmware, downloads and flashes `<FW_BASE_URL><MAC>.bin` via
   `ESP8266httpUpdate`.
5. Continuously redraws an SSD1306 OLED showing Wi-Fi status, MQTT
   status, last update timestamp, firmware update status, and the
   current firmware version.

Both channels are TLS with a pinned root CA: MQTT uses `ROOT_CA_CERT`,
and the OTA/firmware channel uses a separate `OTA_ROOT_CA_CERT` and its
own `WiFiClientSecure` (`otaNet`). `FW_BASE_URL` must be `https://` — an
unauthenticated HTTP OTA channel would let anyone on the network path
push arbitrary code to the device, and `ESP8266httpUpdate` on its own
does not verify a code-signing signature, so certificate pinning is the
only thing standing between this device and a malicious firmware image.

## Architecture

```mermaid
flowchart LR
    subgraph Device["ESP8266 Node"]
        DHT[DHT22 Sensor]
        MCU[ESP8266 MCU]
        OLED[SSD1306 OLED Display]
        DHT -->|temperature / humidity| MCU
        MCU -->|status text| OLED
    end

    Broker[("MQTT Broker\n(TLS, port 8883)")]
    OTA[("HTTPS Firmware\nUpdate Server")]

    MCU -- "publish: temp / humidity / heartbeat\n(MQTT over TLS)" --> Broker
    Broker -- "subscribe: firmware-update notice\n(MQTT over TLS)" --> MCU
    MCU -- "GET <mac>.version" --> OTA
    OTA -- "200 OK + version string" --> MCU
    MCU -- "GET <mac>.bin (ESP8266httpUpdate)" --> OTA
    OTA -- "firmware binary" --> MCU
```

## Hardware

| Component            | Notes                                   |
|-----------------------|------------------------------------------|
| ESP8266 board          | e.g. NodeMCU v2/v3, Wemos D1 mini        |
| SSD1306 OLED (I2C)     | 0.96" 128x64, address `0x3c`              |
| DHT22 (AM2302)         | Temperature/humidity sensor              |

### Wiring

| Signal        | ESP8266 pin | Notes                     |
|---------------|-------------|---------------------------|
| OLED SDA      | GPIO 5 (D1) | I2C data                  |
| OLED SCL      | GPIO 4 (D2) | I2C clock                 |
| DHT22 data    | GPIO 12 (D6)| Add a 10k pull-up to 3V3  |
| Status LED    | D0          | Onboard blink each loop   |

## Required libraries

- `ESP8266WiFi`, `ESP8266HTTPClient`, `ESP8266httpUpdate` (bundled with
  the ESP8266 Arduino core)
- [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT client)
- [ArduinoJson](https://arduinojson.org/) v7+
- [DHT sensor library](https://github.com/adafruit/DHT-sensor-library) +
  Adafruit Unified Sensor
- [ESP8266 and ESP32 OLED driver for SSD1306 displays](https://github.com/ThingPulse/esp8266-oled-ssd1306)
  (provides `SSD1306Brzo.h`)

## Configuration

Copy `secrets.example.h` to `secrets.h` (git-ignored — never commit real
credentials) and fill in:

- `WIFI_SSID` / `WIFI_PASS`
- `MQTT_HOST`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`
- `FW_BASE_URL` — `https://` base URL the device checks for `.version`/`.bin`
  files
- `ROOT_CA_CERT` — the CA certificate that signed your MQTT broker's TLS
  certificate (`openssl s_client -connect <host>:8883 -showcerts`)
- `OTA_ROOT_CA_CERT` — the CA certificate that signed your OTA/firmware
  server's TLS certificate (same command, against the OTA host/port)

Non-secret settings such as the MQTT topic prefix (`HOSTNAME`) and
`FW_VERSION` live directly in `IoT_ESP8266_MCU_OLED.ino` — bump
`FW_VERSION` on every release you publish to the update server.

## Build & flash

### PlatformIO

```
pio run -t upload
pio device monitor -b 115200
```

`platformio.ini` pins the required library versions for a reproducible
build.

### Arduino IDE

1. Install the libraries listed above via Library Manager.
2. Open `IoT_ESP8266_MCU_OLED.ino` directly (the IDE will pick up
   `secrets.h` from the same sketch folder automatically).
3. Select an ESP8266 board (e.g. "NodeMCU 1.0 (ESP-12E Module)").
4. Upload.

## Verification

This sketch targets ESP8266 hardware and TLS/OTA infrastructure that
isn't available in this environment, and neither `platformio` nor
`arduino-cli` is installed here, so the build could not be compiled or
flashed as part of this change. Review the diff carefully and build
locally with PlatformIO or the Arduino IDE before flashing a device.

## Project statistics

| Metric | Value |
|---|---|
| Tracked files | 5 |
| Lines of code (non-blank) | 373 |
| Languages | C++ (Arduino) 330, C/C++ header 43 |
| Commits | 4 |

CI compiles the firmware with PlatformIO on each push to `master`, using the example credentials file.
