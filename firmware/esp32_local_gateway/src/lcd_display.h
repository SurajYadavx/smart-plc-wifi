#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "config.h"

class LcdDisplay {
public:
    LcdDisplay();
    void begin();
    void showBooting();
    void showConnected(const String& ip);
    void updateStatus(
        const String& ip,
        bool qConnected,
        bool rConnected,
        uint8_t y20State
    );

private:
    LiquidCrystal_I2C lcd;
    bool isAvailable = false;
    uint32_t lastUpdateMs = 0;
};

extern LcdDisplay lcdDisplay;
