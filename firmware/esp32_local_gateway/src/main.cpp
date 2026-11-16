#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "lcd_display.h"
#include "plc_manager.h"
#include "web_server.h"

static uint32_t lastWifiCheckMs = 0;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(200);

    Serial.println();
    Serial.println(F("=================================================================="));
    Serial.println(F("  PROJECT V2 — MITSUBISHI Q/R SERIES DUAL-PLC LOCAL GATEWAY       "));
    Serial.printf ("  Firmware Version: %s | Gateway ID: %s\n", FIRMWARE_VERSION, GATEWAY_ID);
    Serial.println(F("=================================================================="));

    // 1. Initialize LCD (Graceful autodetection: boots headless if not connected)
    lcdDisplay.begin();

    // 2. Initialize Wi-Fi in Station Mode
    Serial.printf("[WIFI] Connecting to SSID: '%s'...\n", SECRET_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 10000)) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected! Assigned IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI] Subnet: %s | Gateway: %s | RSSI: %d dBm\n",
            WiFi.subnetMask().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(),
            WiFi.RSSI()
        );
        lcdDisplay.showConnected(WiFi.localIP().toString());
    } else {
        Serial.println(F("[WIFI] WARNING: Initial connection timed out. Gateway will continue auto-reconnecting."));
    }

    // 3. Initialize Dual-PLC Communication Engine
    plcManager.begin();

    // 4. Initialize Local Web Server and REST API
    gatewayWebServer.begin();

    Serial.println(F("[BOOT] Ready! Local Supervisory System operational on port 80."));
    Serial.printf("[BOOT] Open browser to: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.println(F("=================================================================="));
}

void loop() {
    uint32_t now = millis();

    // Periodic Wi-Fi reconnection check
    if (now - lastWifiCheckMs >= 5000) {
        lastWifiCheckMs = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[WIFI] Disconnected — attempting reconnection..."));
            WiFi.reconnect();
        }
    }

    // 1. Handle HTTP client requests
    gatewayWebServer.loop();

    // 2. Run background PLC telemetry acquisition
    plcManager.loop();

    // 3. Refresh hardware LCD status if installed
    const auto& q = plcManager.getQTelemetry();
    uint8_t y20Val = q.hasValidSample ? q.outputs[0] : 0;
    lcdDisplay.updateStatus(
        WiFi.localIP().toString(),
        plcManager.isQConnected(),
        plcManager.isRConnected(),
        y20Val
    );

    delay(1); // Cooperative multitasking yield
}
