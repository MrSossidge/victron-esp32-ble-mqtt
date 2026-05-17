#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "victron_ble.h"

// ── Globals ──────────────────────────────────────────────────────────────────

static uint8_t s_encKey[16];

// Shared between BLE core (1) and MQTT core (0)
static volatile bool s_newData = false;
static MpptData      s_latest  = {};

// MQTT + WiFi — only touched by core 0 task
static WiFiClient   s_wifiClient;
static PubSubClient s_mqtt(s_wifiClient);
static bool         s_discoveryPublished = false;

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool hexToBin(const char* hex, uint8_t* out, size_t outLen) {
  if (strlen(hex) != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    auto v = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int h = v(hex[i*2]), l = v(hex[i*2+1]);
    if (h < 0 || l < 0) return false;
    out[i] = (uint8_t)((h << 4) | l);
  }
  return true;
}

// ── WiFi ─────────────────────────────────────────────────────────────────────

static void wifiConnect() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Failed, will retry");
  }
}

// ── MQTT ─────────────────────────────────────────────────────────────────────

static void mqttConnect() {
  while (!s_mqtt.connected()) {
    Serial.print("[MQTT] Connecting...");
    bool ok;
    if (strlen(MQTT_USER) > 0) {
      ok = s_mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                          MQTT_TOPIC_STATUS, 0, true, "offline");
    } else {
      ok = s_mqtt.connect(MQTT_CLIENT_ID, nullptr, nullptr,
                          MQTT_TOPIC_STATUS, 0, true, "offline");
    }
    if (ok) {
      Serial.println(" connected");
      s_mqtt.publish(MQTT_TOPIC_STATUS, "online", true);
      s_discoveryPublished = false;
    } else {
      Serial.printf(" failed (rc=%d), retry in 5s\n", s_mqtt.state());
      delay(5000);
    }
  }
}

// ── Home Assistant Discovery ──────────────────────────────────────────────────
// Payloads are hardcoded strings in flash — no runtime String building,
// no buffer size surprises. Each is well under 600 bytes total.

static void publishDiscovery() {

  // Common device suffix appended to every payload
  // identifiers must match across all sensors for HA to group them
  #define DEV  ",\"device\":{\"identifiers\":[\"victron_smartsolar_garage\"],\"name\":\"Victron SmartSolar MPPT\",\"model\":\"SmartSolar MPPT 100/20\",\"manufacturer\":\"Victron Energy\"}}"
  #define AVAIL ",\"availability_topic\":\"" MQTT_TOPIC_STATUS "\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\""

  struct { const char* topic; const char* payload; } sensors[] = {
    {
      "homeassistant/sensor/victron_smartsolar_garage/battery_voltage/config",
      "{\"unique_id\":\"victron_mppt_bv\",\"name\":\"Battery Voltage\","
      "\"state_topic\":\"" MQTT_TOPIC_DATA "\","
      "\"value_template\":\"{{ value_json.battery_voltage | float }}\","
      "\"unit_of_measurement\":\"V\",\"device_class\":\"voltage\","
      "\"state_class\":\"measurement\"" AVAIL DEV
    },
    {
      "homeassistant/sensor/victron_smartsolar_garage/battery_current/config",
      "{\"unique_id\":\"victron_mppt_bc\",\"name\":\"Battery Current\","
      "\"state_topic\":\"" MQTT_TOPIC_DATA "\","
      "\"value_template\":\"{{ value_json.battery_current | float }}\","
      "\"unit_of_measurement\":\"A\",\"device_class\":\"current\","
      "\"state_class\":\"measurement\"" AVAIL DEV
    },
    {
      "homeassistant/sensor/victron_smartsolar_garage/pv_power/config",
      "{\"unique_id\":\"victron_mppt_pv\",\"name\":\"PV Power\","
      "\"state_topic\":\"" MQTT_TOPIC_DATA "\","
      "\"value_template\":\"{{ value_json.pv_power | float }}\","
      "\"unit_of_measurement\":\"W\",\"device_class\":\"power\","
      "\"state_class\":\"measurement\"" AVAIL DEV
    },
    {
      "homeassistant/sensor/victron_smartsolar_garage/yield_today/config",
      "{\"unique_id\":\"victron_mppt_yt\",\"name\":\"Yield Today\","
      "\"state_topic\":\"" MQTT_TOPIC_DATA "\","
      "\"value_template\":\"{{ value_json.yield_today_wh | float }}\","
      "\"unit_of_measurement\":\"Wh\",\"device_class\":\"energy\","
      "\"state_class\":\"total_increasing\"" AVAIL DEV
    },
    {
      "homeassistant/sensor/victron_smartsolar_garage/charge_state/config",
      "{\"unique_id\":\"victron_mppt_cs\",\"name\":\"Charge State\","
      "\"state_topic\":\"" MQTT_TOPIC_DATA "\","
      "\"value_template\":\"{{ value_json.charge_state }}\","
      "\"icon\":\"mdi:solar-power\"" AVAIL DEV
    },
    {
      "homeassistant/sensor/victron_smartsolar_garage/error_code/config",
      "{\"unique_id\":\"victron_mppt_ec\",\"name\":\"Error Code\","
      "\"state_topic\":\"" MQTT_TOPIC_DATA "\","
      "\"value_template\":\"{{ value_json.error_code | int }}\","
      "\"icon\":\"mdi:alert-circle-outline\"" AVAIL DEV
    },
  };

  for (auto& s : sensors) {
    bool ok = s_mqtt.publish(s.topic, s.payload, true);
    Serial.printf("[HA] %s: %s (%d bytes)\n",
                  ok ? "OK" : "FAIL", s.topic, strlen(s.payload));
    delay(50);
  }

  s_discoveryPublished = true;
  Serial.println("[HA] Discovery complete");

  #undef DEV
  #undef AVAIL
}

// ── Data Publisher ────────────────────────────────────────────────────────────

static void publishData(const MpptData& d) {
  StaticJsonDocument<256> doc;
  doc["battery_voltage"]  = serialized(String(d.batteryVoltage, 2));
  doc["battery_current"]  = serialized(String(d.batteryCurrent, 2));
  doc["pv_power"]         = serialized(String(d.pvPower, 1));
  doc["yield_today_wh"]   = serialized(String(d.yieldToday, 0));
  doc["charge_state"]     = chargeStateName(d.chargeState);
  doc["charge_state_raw"] = d.chargeState;
  doc["error_code"]       = d.errorCode;

  char buf[256];
  serializeJson(doc, buf);
  s_mqtt.publish(MQTT_TOPIC_DATA, buf, false);
  Serial.printf("[MQTT] Published: %s\n", buf);
}

// ── MQTT Task (runs on Core 0) ────────────────────────────────────────────────

void mqttTask(void* param) {
  delay(1000);

  // Buffer must be set BEFORE connect — 620 bytes covers our largest payload
  s_mqtt.setBufferSize(620);
  s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  s_mqtt.setKeepAlive(60);

  wifiConnect();
  mqttConnect();

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) wifiConnect();
    if (!s_mqtt.connected()) mqttConnect();

    s_mqtt.loop();

    if (!s_discoveryPublished) publishDiscovery();

    if (s_newData) {
      s_newData = false;
      publishData(s_latest);
    }

    delay(100);
  }
}

// ── BLE Advertised Device Callback (fires on Core 1) ─────────────────────────

class VictronAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* device) override {
    NimBLEAddress targetAddr(VICTRON_MAC);
    if (device->getAddress() != targetAddr) return;
    if (!device->haveManufacturerData()) return;

    std::string mfr = device->getManufacturerData();
    if (mfr.size() < 6 || (uint8_t)mfr[0] != 0xE1 || (uint8_t)mfr[1] != 0x02) return;

    const uint8_t* payload = (const uint8_t*)mfr.data() + 2;
    size_t payloadLen       = mfr.size() - 2;

    MpptData parsed = parseVictronMppt(s_encKey, payload, payloadLen);
    if (parsed.valid) {
      s_latest  = parsed;
      s_newData = true;
      Serial.println("[BLE] Valid Victron packet decoded");
    }
  }
};

// ── BLE Scanner (runs on Core 1 via loop()) ───────────────────────────────────

static NimBLEScan* s_scan = nullptr;

static void bleInit() {
  NimBLEDevice::init("esp32-victron");
  s_scan = NimBLEDevice::getScan();
  s_scan->setAdvertisedDeviceCallbacks(new VictronAdvertisedDeviceCallbacks(), true);
  s_scan->setActiveScan(false);
  s_scan->setInterval(100);
  s_scan->setWindow(99);
  Serial.println("[BLE] Scanner initialised");
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[Boot] Victron ESP32 MQTT bridge starting");

  if (!hexToBin(VICTRON_ENC_KEY, s_encKey, 16)) {
    Serial.println("[ERROR] Bad encryption key — must be 32 hex chars");
    while (true) delay(1000);
  }
  Serial.println("[Boot] Config OK");

  xTaskCreatePinnedToCore(mqttTask, "mqttTask", 8192, NULL, 1, NULL, 0);
  bleInit();
}

void loop() {
  Serial.println("[BLE] Scanning...");
  s_scan->start(BLE_SCAN_DURATION_S, false);
  s_scan->clearResults();
  delay(PUBLISH_INTERVAL_MS);
}