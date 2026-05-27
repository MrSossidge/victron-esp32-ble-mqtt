# Victron SmartSolar BLE to MQTT Bridge

An ESP32 firmware that reads a **Victron SmartSolar MPPT** charge controller via Bluetooth LE and publishes data to an MQTT broker, with full **Home Assistant auto-discovery**.

Built as a low-power replacement for a Raspberry Pi 4 running the Python `victron-ble` library — dropping idle power consumption from ~4W to ~0.15W.

---

## Features

- Reads Victron SmartSolar MPPT BLE advertisements
- AES-128-CTR decryption matching the official Victron BLE protocol
- Publishes JSON data to MQTT every 10 seconds
- Full Home Assistant MQTT auto-discovery — device appears automatically with all entities grouped
- LWT (Last Will and Testament) online/offline status
- Dual-core FreeRTOS — BLE scanning on core 1, WiFi/MQTT on core 0, no coexistence crashes
- Passive BLE scan — lower power draw

---

## Hardware

- ESP32 dev board (tested on ESP32 CH340 38-pin)
- Within BLE range of your Victron SmartSolar MPPT (~10m)
- WiFi network

> **Note:** The ESP32-C3 Super Mini should also work but requires the BOOT button to be held during flashing — the CH340 classic ESP32 is easier to work with.

---

## Getting Started

### 1. Clone the repo

```bash
git clone https://github.com/mrsossidge/victron-esp32-ble-mqtt.git
cd victron-esp32-ble-mqtt
```

### 2. Install VS Code + PlatformIO

- Install [VS Code](https://code.visualstudio.com)
- Install the **PlatformIO IDE** extension from the marketplace
- Open the project folder — PlatformIO will detect `platformio.ini` and install dependencies automatically

### 3. Configure

Copy the example config and fill in your details:

```bash
cp include/example.config.h include/config.h
```

Edit `include/config.h`:

```cpp
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASSWORD   "your_wifi_password"
#define MQTT_HOST       "192.168.1.x"        // your MQTT broker IP
#define VICTRON_MAC     "xx:xx:xx:xx:xx:xx"  // lowercase, colon-separated
#define VICTRON_ENC_KEY "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // 32 hex chars
```

#### Finding your Victron MAC and encryption key

1. Open the **Victron Connect** app
2. Connect to your SmartSolar MPPT
3. Tap the **⋮ menu** → **Product Info**
4. Scroll down to **Instant readout via Bluetooth**
5. Tap **SHOW** — your encryption key is displayed there
6. The MAC address is shown at the top of the Product Info page

### 4. Build and upload

- Press `Ctrl+Alt+B` to build
- Press `Ctrl+Alt+U` to upload
- Open the serial monitor with `Ctrl+Alt+S` to verify it's running

---

## MQTT Topics

| Topic | Description |
|---|---|
| `solar/mppt/data` | JSON payload with all sensor values |
| `solar/mppt/status` | `online` / `offline` (retained LWT) |

### Example payload

```json
{
  "battery_voltage": "12.98",
  "battery_current": "-0.20",
  "pv_power": "8.0",
  "yield_today_wh": "350",
  "charge_state": "bulk",
  "charge_state_raw": 3,
  "error_code": 0
}
```

### Charge states

| Value | State |
|---|---|
| 0 | Off |
| 2 | Fault |
| 3 | Bulk |
| 4 | Absorption |
| 5 | Float |
| 7 | Equalize |
| 245 | Starting |
| 252 | External control |

---

## Home Assistant

The device auto-discovers in Home Assistant via MQTT discovery. Once the ESP32 is running, go to:

**Settings → Devices & Services → MQTT**

You should see a **Victron SmartSolar MPPT** device with these entities:

- Battery Voltage (V)
- Battery Current (A)
- PV Power (W)
- Yield Today (Wh)
- Charge State
- Error Code

No manual YAML configuration required.

---

## Power Consumption

| Device | Idle draw |
|---|---|
| Raspberry Pi 4 | ~3–5W |
| ESP32 (this firmware) | ~0.15W |

Saving ~4W overnight adds up to ~40Wh per 10 hour night, or roughly 3–4Ah at 12V — meaningful for a solar battery system.

---

## Project Structure

```
├── include/
│   ├── config.h          # Your config (excluded from git)
│   ├── example.config.h  # Template — copy to config.h
│   └── victron_ble.h     # BLE parser header
├── src/
│   ├── main.cpp          # Main firmware — WiFi, MQTT, BLE scanning
│   └── victron_ble.cpp   # AES decryption and MPPT data parser
└── platformio.ini        # PlatformIO build config
```

---

## How it works

Victron SmartSolar MPPTs broadcast BLE advertisements every few seconds containing encrypted sensor data. The encryption uses AES-128-CTR with:

- Key: your device's 32-character hex encryption key
- IV: a 16-bit nonce from the advertisement, used as a 128-bit little-endian counter initial value

Once decrypted, the payload is a bit-packed structure read LSB-first — this implementation matches the [victron-ble](https://github.com/keshavdv/victron-ble) Python library exactly.

The ESP32 runs BLE scanning on core 1 and WiFi/MQTT on core 0 using FreeRTOS tasks, avoiding the radio coexistence issues that cause crashes when both run on the same core.

---

## Dependencies

Managed automatically by PlatformIO:

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — lightweight BLE stack
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- [ArduinoJson](https://arduinojson.org) — JSON serialisation
- mbedtls — AES decryption (bundled with ESP32 Arduino core)

---

## Related Projects

- [victron-ble](https://github.com/keshavdv/victron-ble) — Python library this is based on (great for Pi/Linux setups)
- [esphome-victron_ble](https://github.com/Fabian-Schmidt/esphome-victron_ble) — ESPHome component if you prefer YAML config
- [Victron_BLE_Advertising_example](https://github.com/hoberman/Victron_BLE_Advertising_example) — original Arduino BLE example

---

## Licence

MIT

---

## Home Assistant Dashboard

A ready-made dashboard card is included in `home-assistant/dashboard.yaml`.

Requires these HACS frontend plugins:
- [Mushroom](https://github.com/piitaya/lovelace-mushroom)
- [Mini Graph Card](https://github.com/kalkih/mini-graph-card)

To use: **Dashboard → Edit → Add Card → Manual** and paste the contents.

## Template Sensors

Optional calculated sensors are in `home-assistant/configuration_template.yaml`:

- **Garage Net Battery Power** — battery current × voltage in watts (positive = charging)
- **Garage Battery Status** — Charging / Discharging / Idle text sensor

Add the `template:` block to your `configuration.yaml` and reload YAML.

## Project Structure

```
├── home-assistant/
│   ├── dashboard.yaml               # Ready-made HA dashboard card
│   └── configuration_template.yaml  # Optional template sensors
├── include/
│   ├── config.h                     # Your config (excluded from git)
│   ├── example.config.h             # Template — copy to config.h
│   └── victron_ble.h                # BLE parser header
├── src/
│   ├── main.cpp                     # Main firmware
│   └── victron_ble.cpp              # AES decryption and parser
└── platformio.ini                   # PlatformIO build config
```
<a href="https://www.buymeacoffee.com/MrSossidge" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" style="height: 60px !important;width: 217px !important;" ></a>
