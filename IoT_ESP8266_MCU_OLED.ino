// IoT_ESP8266_MCU_OLED.ino
//
// ESP8266 sensor node that:
//   - reads temperature/humidity from a DHT22 sensor
//   - publishes readings to an MQTT broker over TLS
//   - shows Wi-Fi/MQTT/update status plus the last publish time on an
//     SSD1306 OLED display
//   - listens on MQTT for a firmware-update notification and, when a
//     newer version is announced, performs an HTTPS OTA update (the
//     firmware server's certificate is pinned via OTA_ROOT_CA_CERT)
//
// Libraries required (install via Arduino Library Manager or PlatformIO):
//   ESP8266WiFi, ESP8266HTTPClient, ESP8266httpUpdate (bundled with the
//     ESP8266 Arduino core)
//   PubSubClient      (Nick O'Leary)
//   ArduinoJson        >= 7.0
//   DHT sensor library (Adafruit) + Adafruit Unified Sensor
//   esp8266-oled-ssd1306 (ThingPulse / Daniel Eichhorn) for SSD1306Brzo
//
// Configuration:
//   Copy secrets.example.h to secrets.h (git-ignored) and fill in your
//   Wi-Fi credentials, MQTT broker details and root CA certificate.

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include "SSD1306Brzo.h"

#include "secrets.h"

// ---------------------------------------------------------------------
// Device / topic configuration (non-secret)
// ---------------------------------------------------------------------
#define HOSTNAME "/IoT/oled/Suzukake/01"

// Bump FW_VERSION on every release. It's published (as a string) to
// MQTT_PUB_TOPIC_FW so subscribers can display it.
static const int FW_VERSION = 12349;

static const char MQTT_SUB_TOPIC[]          = "IoT/Firmware_Update/in";
static const char MQTT_SUB_TOPIC_FW_UPDATE[] = "SH_Gateway/fw_update";
static const char MQTT_PUB_TOPIC[]          = HOSTNAME "/out";
static const char MQTT_PUB_TOPIC_FW[]       = HOSTNAME "/fw";

#define HUMIDITY_TOPIC             HOSTNAME "/humidity"
#define TEMPERATURE_CELSIUS_TOPIC  HOSTNAME "/temperature_c"
#define TEMPERATURE_FAHRENHEIT_TOPIC HOSTNAME "/temperature_f"

#define DHT_PIN  12
#define DHT_TYPE DHT22
#define STATUS_LED D0

static const unsigned long PUBLISH_INTERVAL_MS = 5000;

// Local UTC offset used for the SNTP time sync in setup() (JST, UTC+9).
static const long TIME_ZONE_OFFSET_SEC = 9 * 3600;

// A timestamp from 2017-11-13; time(nullptr) returning anything before
// this means SNTP hasn't synced yet (the RTC starts at epoch 0 on boot).
static const time_t SNTP_SYNCED_THRESHOLD = 1510592825;

// ---------------------------------------------------------------------
// Peripherals
// ---------------------------------------------------------------------
SSD1306Brzo display(0x3c, /* SDA */ 5, /* SCL */ 4);
DHT dht(DHT_PIN, DHT_TYPE);

BearSSL::WiFiClientSecure net;
PubSubClient client(net);

// Separate TLS client (with its own trust anchor) used only for OTA
// firmware checks/downloads. Kept distinct from the MQTT client `net`
// so an in-progress OTA fetch never shares/steals the MQTT connection's
// TLS session state.
BearSSL::WiFiClientSecure otaNet;

// ---------------------------------------------------------------------
// OLED status text (updated by the various state-change handlers below)
// ---------------------------------------------------------------------
String wifiStatusText   = "Connecting...";
String mqttStatusText   = "Connecting...";
String lastUpdateText   = "";
String updateStatusText = "";

time_t now = 0;
unsigned long lastPublishMillis = 0;

// ---------------------------------------------------------------------
// OLED helpers
// ---------------------------------------------------------------------
void redrawDisplay() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "WiFi Status:");
  display.drawString(55, 0, wifiStatusText);
  display.drawString(0, 10, "MQTT Status:");
  display.drawString(65, 10, mqttStatusText);
  display.drawString(0, 20, "Last update:");
  display.drawString(0, 30, lastUpdateText);
  display.drawString(0, 40, "Firmware: ");
  display.drawString(45, 40, updateStatusText);
  display.drawString(0, 50, "FW Ver: " + String(FW_VERSION));
  display.display();
}

void setWifiConnecting() { wifiStatusText = "Connecting..."; }
void setWifiOk()          { wifiStatusText = " [ OK ]"; }

void setMqttWaiting() {
  mqttStatusText = "Waiting...";
  lastUpdateText = "";
}
void setMqttOk() { mqttStatusText = " [ OK ]"; }

void updateCurrentTimeText() {
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  lastUpdateText = asctime(&timeinfo);
}

void setUpdateChecking()  { updateStatusText = " [ Checking ]"; }
void setUpdateLatest()    { updateStatusText = " [ Latest ]"; }
void setUpdatePreparing() { updateStatusText = " [ Preparing.. ]"; }
void setUpdateError()     { updateStatusText = " [ Error ! ]"; }

// ---------------------------------------------------------------------
// OTA update check
// ---------------------------------------------------------------------
void checkForUpdates() {
  String mac = WiFi.macAddress();
  String fwBaseUrl = String(FW_BASE_URL) + mac;
  String fwVersionUrl = fwBaseUrl + ".version";

  Serial.println("Checking for firmware updates.");
  Serial.print("MAC address: ");
  Serial.println(mac);
  Serial.print("Firmware version URL: ");
  Serial.println(fwVersionUrl);

  HTTPClient httpClient;
  // Use the TLS client (with OTA_ROOT_CA_CERT pinned as trust anchor) so
  // the version string can't be spoofed/downgraded by a MITM either.
  httpClient.begin(otaNet, fwVersionUrl);
  int httpCode = httpClient.GET();

  if (httpCode == 200) {
    String newFWVersionStr = httpClient.getString();
    setUpdateChecking();
    redrawDisplay();

    Serial.print("Current firmware version: ");
    Serial.println(FW_VERSION);
    Serial.print("Available firmware version: ");
    Serial.println(newFWVersionStr);

    int newFWVersion = newFWVersionStr.toInt();

    if (newFWVersion > FW_VERSION) {
      Serial.println("Preparing to update");
      setUpdatePreparing();
      redrawDisplay();

      String fwImageUrl = fwBaseUrl + ".bin";
      t_httpUpdate_return ret = ESPhttpUpdate.update(otaNet, fwImageUrl);

      switch (ret) {
        case HTTP_UPDATE_FAILED:
          Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n",
                        ESPhttpUpdate.getLastError(),
                        ESPhttpUpdate.getLastErrorString().c_str());
          setUpdateError();
          redrawDisplay();
          break;

        case HTTP_UPDATE_NO_UPDATES:
          Serial.println("HTTP_UPDATE_NO_UPDATES");
          setUpdateError();
          redrawDisplay();
          break;

        case HTTP_UPDATE_OK:
          Serial.println("HTTP_UPDATE_OK (device will reboot)");
          break;
      }
    } else {
      Serial.println("Already on latest version");
      setUpdateLatest();
      redrawDisplay();
    }
  } else {
    Serial.print("Firmware version check failed, got HTTP response code ");
    Serial.println(httpCode);
    setUpdateError();
    redrawDisplay();
  }

  httpClient.end();
}

// ---------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------
void mqttConnect() {
  while (!client.connected()) {
    Serial.print("Time: ");
    Serial.print(ctime(&now));
    Serial.print("MQTT connecting ... ");

    if (client.connect(HOSTNAME, MQTT_USER, MQTT_PASS)) {
      Serial.println("connected.");
      setMqttOk();
      redrawDisplay();
      client.subscribe(MQTT_SUB_TOPIC);
      client.subscribe(MQTT_SUB_TOPIC_FW_UPDATE);
    } else {
      setMqttWaiting();
      redrawDisplay();
      Serial.print("failed, status code =");
      Serial.print(client.state());
      Serial.println(". Try again in 5 seconds.");
      delay(5000);
    }
  }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.print("Failed to parse firmware-update JSON: ");
    Serial.println(err.c_str());
    return;
  }

  const int fwVersion = doc["fw_version"] | -1;
  const char* fwUrl = doc["fw_url"] | "";
  Serial.println(topic);
  Serial.println(fwVersion);
  Serial.println(fwUrl);

  if (fwVersion > FW_VERSION) {
    Serial.println("New version detected!");
    checkForUpdates();
  }
}

// ---------------------------------------------------------------------
// Sensor publish
// ---------------------------------------------------------------------
void publishSensorReadings() {
  client.publish(MQTT_PUB_TOPIC, ctime(&now), false);
  client.publish(MQTT_PUB_TOPIC_FW, String(FW_VERSION).c_str(), false);

  // Reading temperature or humidity takes about 250ms; sensor readings
  // may be up to 2 seconds "old" (it's a slow sensor).
  float humidity = dht.readHumidity();
  float tempC = dht.readTemperature();
  float tempF = dht.readTemperature(true);

  if (isnan(humidity) || isnan(tempC) || isnan(tempF)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.print("Temperature in Celsius:");
  Serial.println(String(tempC).c_str());
  client.publish(TEMPERATURE_CELSIUS_TOPIC, String(tempC).c_str(), true);

  Serial.print("Temperature in Fahrenheit:");
  Serial.println(String(tempF).c_str());
  client.publish(TEMPERATURE_FAHRENHEIT_TOPIC, String(tempF).c_str(), true);

  Serial.print("Humidity:");
  Serial.println(String(humidity).c_str());
  client.publish(HUMIDITY_TOPIC, String(humidity).c_str(), true);
}

// ---------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------
void setup() {
  display.init();
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.print("Attempting to connect to SSID: ");
  Serial.print(WIFI_SSID);
  Serial.print("  ");

  WiFi.hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    setWifiConnecting();
    redrawDisplay();
    Serial.print(".");
    delay(1000);
  }
  setWifiOk();
  redrawDisplay();

  Serial.println("  CONNECTED !");
  Serial.print("IoT device IP -->  ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.print("Firmware Version --> ");
  Serial.println(FW_VERSION);
  Serial.println();
  Serial.print("IoT device MAC --> ");
  Serial.println(WiFi.macAddress());
  Serial.println();

  Serial.print("Setting time using SNTP -->  ");
  configTime(TIME_ZONE_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < SNTP_SYNCED_THRESHOLD) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("  OK");
  Serial.println();

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));

  pinMode(STATUS_LED, OUTPUT);

  // Root CA used to validate the MQTT broker's TLS certificate.
  // NOTE: must be `static` (or otherwise outlive setup()) — setTrustAnchors()
  // only stores a pointer, so a plain stack-local X509List here would be
  // destroyed the moment setup() returns, leaving a dangling trust anchor
  // for the rest of the sketch's life.
  static BearSSL::X509List cert(ROOT_CA_CERT);
  net.setTrustAnchors(&cert);

  // Root CA used to validate the OTA/firmware server's TLS certificate.
  // Pinning this (rather than calling otaNet.setInsecure()) is what
  // stops a MITM from serving a malicious .bin during an update check.
  static BearSSL::X509List otaCert(OTA_ROOT_CA_CERT);
  otaNet.setTrustAnchors(&otaCert);

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(onMqttMessage);
  mqttConnect();
  checkForUpdates();
}

void loop() {
  updateCurrentTimeText();
  redrawDisplay();
  now = time(nullptr);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Checking wifi");
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      Serial.print(".");
      delay(10);
    }
    Serial.println("connected");
  } else if (!client.connected()) {
    mqttConnect();
  } else {
    client.loop();
  }

  if (millis() - lastPublishMillis > PUBLISH_INTERVAL_MS) {
    lastPublishMillis = millis();
    publishSensorReadings();
  }

  digitalWrite(STATUS_LED, HIGH);
  delay(1000);
  digitalWrite(STATUS_LED, LOW);
  delay(30);
}
