/**
 * esp32_local_gateway.ino
 * ────────────────────────
 * Arduino IDE Compatible Entry Point for the Q/R Series Dual-PLC Local Gateway.
 * 
 * Target Board: ESP32 Dev Module
 * Flash Size: 4MB (Partition Scheme: "Default 4MB with spiffs/littlefs" or "Minimal SPIFFS")
 * Core: Arduino ESP32 >= 2.0.x
 * 
 * Required Libraries:
 *   - ArduinoJson (v7.x by Benoit Blanchon)
 *   - LiquidCrystal_I2C (by Frank de Brabander or Marco Schwartz)
 * 
 * Filesystem Upload:
 *   Use the "ESP32 LittleFS Data Upload" tool in Arduino IDE to upload
 *   the 'data/' directory containing the web dashboard.
 */

#include "src/config.h"
#include "src/mc_protocol.h"
#include "src/plc_manager.h"
#include "src/lcd_display.h"
#include "src/web_server.h"

// Forward to main.cpp setup() and loop() implementations
extern void setup();
extern void loop();
