// secrets.example.h
//
// Copy this file to "secrets.h" (which is git-ignored) and fill in your
// own values. secrets.h is #included by IoT_ESP8266_MCU_OLED.ino and must
// live in the same directory as the sketch.

#pragma once

// ---- Wi-Fi -----------------------------------------------------------
static const char WIFI_SSID[] = "YOUR SSID";
static const char WIFI_PASS[] = "YOUR PASSWORD";

// ---- MQTT broker (TLS) ------------------------------------------------
static const char MQTT_HOST[] = "192.168.4.1";
static const int  MQTT_PORT   = 8883;
static const char MQTT_USER[] = "iot";   // leave blank if the broker needs no auth
static const char MQTT_PASS[] = "iot";

// ---- HTTPS/HTTP OTA update server -------------------------------------
// Firmware binaries and ".version" files are expected at:
//   <FW_BASE_URL><MAC-address>.version
//   <FW_BASE_URL><MAC-address>.bin
static const char FW_BASE_URL[] = "http://192.168.4.1/firmwares/";

// ---- TLS root CA used to validate the MQTT broker's certificate -------
// Generate with, e.g.:
//   openssl s_client -connect <host>:8883 -showcerts
// or copy the CA/root certificate that signed the broker's cert.
static const char ROOT_CA_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
REPLACE WITH YOUR CA CERTIFICATE
-----END CERTIFICATE-----
)EOF";
