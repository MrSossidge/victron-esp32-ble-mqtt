#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────────────
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASSWORD   "your_wifi_password"

// ── MQTT broker ───────────────────────────────────────────────────────────────
#define MQTT_HOST       "192.168.1.x"       // IP address of your MQTT broker
#define MQTT_PORT       1883
#define MQTT_CLIENT_ID  "esp32-victron"
// Leave blank if your broker has no auth:
#define MQTT_USER       ""
#define MQTT_PASSWORD   ""

// ── MQTT topics ───────────────────────────────────────────────────────────────
#define MQTT_TOPIC_DATA   "solar/mppt/data"
#define MQTT_TOPIC_STATUS "solar/mppt/status"

// ── Victron SmartSolar MPPT ───────────────────────────────────────────────────
// MAC address of your Victron device (lowercase, colon-separated)
// Find it in Victron Connect app → device → top-right menu → Product Info
#define VICTRON_MAC     "xx:xx:xx:xx:xx:xx"

// 32-character hex encryption key
// Find it in Victron Connect app → device → top-right menu → Product Info → Instant readout details
#define VICTRON_ENC_KEY "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

// ── Timing ────────────────────────────────────────────────────────────────────
// How often to publish to MQTT (milliseconds)
#define PUBLISH_INTERVAL_MS  10000

// BLE scan window (seconds)
#define BLE_SCAN_DURATION_S  5
