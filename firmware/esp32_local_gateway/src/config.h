#pragma once

#include <Arduino.h>

// Attempt to load private credentials if secrets.h exists
#if __has_include("secrets.h")
  #include "secrets.h"
#elif __has_include("../include/secrets.h")
  #include "../include/secrets.h"
#endif

// Fallback Wi-Fi Credentials for isolated test router
#ifndef SECRET_WIFI_SSID
  #define SECRET_WIFI_SSID "RUBY"
#endif

#ifndef SECRET_WIFI_PASSWORD
  #define SECRET_WIFI_PASSWORD "placeholder_password"
#endif

// SAFETY HARDENING (V2.1): No default operator PIN!
// Missing, empty or placeholder PIN must fail closed.
#ifndef SECRET_OPERATOR_PIN
  #define SECRET_OPERATOR_PIN ""
#endif

// ─── Firmware Identity ─────────────────────────────────────────────────────────
#define GATEWAY_ID           "esp32-gw-01"
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "2.1.0"
#endif
#define SERIAL_BAUD_RATE     115200

// ─── Station 1: Mitsubishi Q-Series Configuration ─────────────────────────────
#define Q_PLC_ENABLED        true
#define Q_PLC_ID             "melsec_q03"
#define Q_PLC_NAME           "Station 1 — Mitsubishi MELSEC-Q"
#define Q_PLC_MODEL          "Q03UDECPU"
#define Q_PLC_IP             "192.168.3.30"
#define Q_PLC_PORT           5010
#define Q_PLC_TIMEOUT_MS     3000
#define Q_PLC_INPUT_COUNT    8     // X0 to X7
#define Q_PLC_OUTPUT_COUNT   8     // Y20 to Y27 (Base hex 0x20)
#define Q_PLC_OUTPUT_BASE    0x20

// Safety Whitelist: Only Y20 permitted as candidate control point
#define Q_PERMITTED_OUTPUT_ADDR 0x20 // Y20

// SAFETY HARDENING (V2.1): Physical writes disabled by default (Fail-Closed)
// Must remain false unless deliberate local commissioning enables after safety approval
#define Q_PLC_WRITES_ENABLED false

// ─── Station 2: Mitsubishi R-Series Configuration (Provisional) ───────────────
#define R_PLC_ENABLED        true
#define R_PLC_ID             "melsec_r04"
#define R_PLC_NAME           "Station 2 — Mitsubishi MELSEC iQ-R"
#define R_PLC_MODEL          "R04CPU / R04ENCPU (Provisional)"
#define R_PLC_IP             "192.168.3.20"
#define R_PLC_PORT           5010
#define R_PLC_TIMEOUT_MS     3000
#define R_PLC_INPUT_COUNT    8     // X0 to X7 (Provisional)
#define R_PLC_OUTPUT_COUNT   8     // Y20 to Y27 (Provisional)
#define R_PLC_OUTPUT_BASE    0x20

// SAFETY HARDENING (V2.1): R-Series writes disabled by default until hardware verified
#define R_PLC_WRITES_ENABLED false

// ─── I2C LCD Display Configuration ────────────────────────────────────────────
#define LCD_I2C_ADDR         0x27
#define LCD_COLS             16
#define LCD_ROWS             2
#define I2C_SDA_PIN          21
#define I2C_SCL_PIN          22

// ─── Polling and Stale Telemetry Thresholds ────────────────────────────────────
#define PLC_POLL_INTERVAL_MS 200   // 200 ms non-blocking background polling
#define STALE_THRESHOLD_MS   10000 // Flag stale after 10s without valid sample
