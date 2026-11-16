# ESP32 Dual-PLC Local Gateway Firmware (Project V2)

**Standalone Embedded Gateway for Mitsubishi MELSEC Q-Series & iQ-R Series PLCs**

---

## 1. Overview

The ESP32 Local Gateway serves as the autonomous edge communication hub in the Project V2 architecture:
- Connects to the local TP-Link router (`RUBY`, 2.4 GHz) via Wi-Fi Station mode.
- Communicates directly over local TCP/IP with:
  - **Station 1:** Mitsubishi Q03UDECPU (`192.168.3.30:5010`) using MC Protocol 3E Binary.
  - **Station 2:** Mitsubishi MELSEC iQ-R (`192.168.3.20:5010`) using SLMP / 3E Binary.
- Hosts an embedded HTTP server (port 80) serving:
  - The static single-page industrial supervisory dashboard (stored in LittleFS flash).
  - A real-time REST API (`/api/v1/...`) providing telemetry snapshots, diagnostics, and validated control.
- Executes controlled output actuation with safety allowlist enforcement, strict banning of `Y0`, operator PIN authorization, and post-write read-back verification.
- Completely standalone: **NO cloud MQTT, NO internet connection, and NO laptop runtime dependency required.**

---

## 2. Hardware Architecture & Pinout

| Component | ESP32 Pin / Interface | Target Connection |
|---|---|---|
| **I2C SDA** | GPIO 21 | LCD 16x2 SDA (Address 0x27) |
| **I2C SCL** | GPIO 22 | LCD 16x2 SCL |
| **Serial RX0** | GPIO 3 | USB / CP2102 Programmer |
| **Serial TX0** | GPIO 1 | USB / CP2102 Programmer (115200 baud) |
| **Power VIN** | 5V Input Pin | Regulated 5V DC from 24V-to-5V Buck Converter |
| **GND** | GND Pin | Common DC Ground with Buck Converter |

> [!CAUTION]
> **ELECTRICAL SAFETY:**
> Never connect 24V DC directly to the ESP32. Always step down 24V DC to 5.0V DC using a high-efficiency buck converter.
> Verify 5.0V with a multimeter BEFORE connecting to the ESP32 VIN pin.

---

## 3. Directory Layout

```text
firmware/esp32_local_gateway/
├── platformio.ini              # PlatformIO project configuration
├── esp32_local_gateway.ino     # Arduino IDE entry point
├── README.md                   # This documentation
├── data/                       # LittleFS web assets directory
│   ├── index.html              # Responsive industrial dashboard
│   ├── test.html               # In-browser test harness
│   └── assets/
│       ├── css/style.css       # Workstation themes & components
│       └── js/app.js           # Supervisory application logic
└── src/
    ├── main.cpp                # Core boot & cooperative scheduling loop
    ├── config.h                # Network, PLC IP addresses, and allowlists
    ├── secrets.example.h       # Credentials template
    ├── mc_protocol.h           # MC Protocol 3E binary frame builder/parser
    ├── mc_protocol.cpp
    ├── plc_manager.h           # Background polling & telemetry cache
    ├── plc_manager.cpp
    ├── web_server.h            # HTTP routes, CORS, and LittleFS delivery
    ├── web_server.cpp
    ├── lcd_display.h           # Non-blocking 16x2 I2C display driver
    └── lcd_display.cpp
```

---

## 4. Configuration & Secrets Setup

1. Copy the example secrets file:
   ```bash
   cp src/secrets.example.h src/secrets.h
   ```
2. Open `src/secrets.h` and configure:
   ```cpp
   #define SECRET_WIFI_SSID     "RUBY"
   #define SECRET_WIFI_PASSWORD "your_router_password"
   #define SECRET_OPERATOR_PIN  "1234"
   ```
3. `secrets.h` is automatically excluded from Git commits via `.gitignore`. Never commit plaintext credentials.

---

## 5. Build and Upload Instructions

### Option A: Using PlatformIO (Recommended CLI / VS Code)

1. Open a terminal in `firmware/esp32_local_gateway/`.
2. Compile the firmware:
   ```bash
   pio run
   ```
3. Upload the LittleFS web dashboard files to the ESP32 flash:
   ```bash
   pio run -t uploadfs
   ```
4. Flash the firmware to the ESP32:
   ```bash
   pio run -t upload
   ```
5. Open the serial monitor at 115200 baud:
   ```bash
   pio device monitor -b 115200
   ```

### Option B: Using Arduino IDE (v2.x)

1. Install the **esp32** board package by Espressif (`Tools > Board > Boards Manager`).
2. Select Board: **ESP32 Dev Module**.
3. Partition Scheme: **Default 4MB with spiffs/littlefs** (or 1.2MB App / 1.5MB SPIFFS/LittleFS).
4. Install Required Libraries via Library Manager (`Sketch > Include Library > Manage Libraries`):
   - `ArduinoJson` (v7.x by Benoit Blanchon)
   - `LiquidCrystal_I2C` (by Marco Schwartz or Frank de Brabander)
5. Open `esp32_local_gateway.ino`.
6. Compile and upload the sketch.
7. To upload the `data/` folder, use the **ESP32 LittleFS Data Upload** tool or plugin.

---

## 6. Embedded REST API Reference

All endpoints return standard JSON responses with explicit error statuses and CORS headers enabled.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/v1/system` | Gateway health, Wi-Fi status, IP, uptime, heap |
| `GET` | `/api/v1/devices` | List configured stations (Q03 and R04) |
| `GET` | `/api/v1/devices/melsec_q03` | Station 1 definition, address map, allowlist |
| `GET` | `/api/v1/devices/melsec_q03/telemetry` | Cached latest sample (X0..X7, Y20..Y27) |
| `GET` | `/api/v1/devices/melsec_q03/diagnostics`| Packet counters, latency, socket errors |
| `GET` | `/api/v1/devices/melsec_r04/telemetry` | Station 2 cached telemetry |
| `POST`| `/api/v1/devices/melsec_q03/outputs/Y20/command` | Authorized output control with read-back verification |

### Output Control Request Payload

```json
POST /api/v1/devices/melsec_q03/outputs/Y20/command
Content-Type: application/json

{
  "state": 1,
  "auth_pin": "1234"
}
```

### Response Payload

```json
{
  "success": true,
  "device_id": "melsec_q03",
  "point": "Y20",
  "requested_state": 1,
  "read_back_state": 1,
  "verified": true,
  "status": "OUTPUT_CONFIRMED",
  "error": null
}
```
