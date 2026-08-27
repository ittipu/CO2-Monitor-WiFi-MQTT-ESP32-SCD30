/*
 * CO2 Monitor with WiFi + MQTT (ESP32 + SCD30)
 * -------------------------------------------------
 * Reads CO2, temperature and humidity from a Sensirion SCD30,
 * classifies the air quality level, and publishes everything as a
 * JSON payload to an MQTT broker.
 *
 * Published JSON payload:
 * {
 *   "device_id":   "esp32-co2-01",
 *   "timestamp":   1724740800,          // Unix epoch seconds (UTC, from NTP)
 *   "co2_ppm":     712.4,
 *   "temperature_c": 24.31,
 *   "humidity_pct":  46.87,
 *   "air_quality": "Acceptable level (Fair)"
 * }
 *
 * Libraries required (install via Library Manager):
 *   - Adafruit SCD30
 *   - PubSubClient          (by Nick O'Leary)
 *   - ArduinoJson           (by Benoit Blanchon, v6.x)
 */

#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Adafruit_SCD30.h"

// ==================== USER CONFIGURATION ====================
// --- WiFi ---
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// --- MQTT broker ---
const char* MQTT_HOST     = "mqtt.iotbhai.io";     // e.g. your broker IP or hostname
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_USER     = "";                    // leave "" if broker needs no auth
const char* MQTT_PASSWORD = "";
const char* MQTT_TOPIC    = "iotbhai/co2/data";    // topic to publish to

// --- Device identity ---
const char* DEVICE_ID     = "esp32-co2-01";

// --- NTP (for real timestamps) ---
const char* NTP_SERVER    = "pool.ntp.org";
const long  GMT_OFFSET_SEC      = 0;   // 0 = publish UTC. For local time set your offset.
const int   DAYLIGHT_OFFSET_SEC = 0;

// --- Timing ---
const unsigned long PUBLISH_INTERVAL_MS = 5000;  // how often to read & publish
// ===========================================================

Adafruit_SCD30 scd30;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastPublish = 0;

// Determine the air quality level text based on CO2 concentration (ppm).
const char* airQualityLevel(float co2_reading) {
  if (co2_reading <= 350)  return "Healthy outside air level (Excellent)";
  if (co2_reading <= 600)  return "Healthy indoor climate (Good)";
  if (co2_reading <= 800)  return "Acceptable level (Fair)";
  if (co2_reading <= 1000) return "Ventilation required (Poor)";
  if (co2_reading <= 1200) return "Ventilation necessary (Bad)";
  if (co2_reading <= 2500) return "Negative health effects (Very Bad)";
  return "HAZARDOUS PROLONGED EXPOSURE (DANGER)";  // above 2500 ppm
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting to WiFi \"");
  Serial.print(WIFI_SSID);
  Serial.print("\" ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print(" connected. IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT broker... ");
    // Use the device id as the MQTT client id (must be unique on the broker).
    bool ok;
    if (strlen(MQTT_USER) > 0) {
      ok = mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD);
    } else {
      ok = mqtt.connect(DEVICE_ID);
    }

    if (ok) {
      Serial.println("connected.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 3s");
      delay(3000);
    }
  }
}

// Returns current Unix epoch seconds, or 0 if NTP time is not yet available.
time_t getEpochTime() {
  time_t now = time(nullptr);
  // Before NTP sync, time() returns a small value (seconds since boot in 1970).
  if (now < 100000) return 0;
  return now;
}

void publishReading() {
  float co2  = scd30.CO2;
  float temp = scd30.temperature;
  float hum  = scd30.relative_humidity;
  const char* quality = airQualityLevel(co2);

  // Build the JSON payload.
  StaticJsonDocument<256> doc;
  doc["device_id"]     = DEVICE_ID;
  doc["timestamp"]     = getEpochTime();
  doc["co2_ppm"]       = co2;
  doc["temperature_c"] = temp;
  doc["humidity_pct"]  = hum;
  doc["air_quality"]   = quality;

  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  bool published = mqtt.publish(MQTT_TOPIC, payload, n);

  Serial.print("Publish to \"");
  Serial.print(MQTT_TOPIC);
  Serial.print(published ? "\" OK: " : "\" FAILED: ");
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);  // Wait for Serial Monitor (no-op on most ESP32 boards)
  }

  Serial.println("\nCO2 Monitor (WiFi + MQTT) Initializing...");

  Wire.begin();
  if (!scd30.begin()) {
    Serial.println("Failed to find SCD30 sensor. Check wiring!");
    while (1) {
      delay(10);
    }
  }
  Serial.println("SCD30 sensor found!");

  connectWiFi();

  // Kick off NTP time sync (non-blocking; epoch fills in shortly).
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("NTP time sync requested.");

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
}

void loop() {
  // Keep network links alive.
  connectWiFi();
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  // Read & publish on a fixed interval when new data is ready.
  if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
    if (scd30.dataReady()) {
      if (scd30.read()) {
        lastPublish = millis();
        publishReading();
      } else {
        Serial.println("Error reading sensor data");
      }
    }
  }
}
