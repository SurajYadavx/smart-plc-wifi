#include "lcd_display.h"

LcdDisplay lcdDisplay;

LcdDisplay::LcdDisplay() : lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS) {}

void LcdDisplay::begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(100000); // 100 kHz I2C clock

    // Non-destructive probe of I2C address 0x27
    Wire.beginTransmission(LCD_I2C_ADDR);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
        isAvailable = true;
        lcd.init();
        lcd.backlight();
        lcd.clear();
        Serial.printf("[LCD] Detected 16x2 I2C Display at 0x%02X\n", LCD_I2C_ADDR);
        showBooting();
    } else {
        isAvailable = false;
        Serial.printf("[LCD] No display detected at 0x%02X (I2C error %d) — running headless\n", LCD_I2C_ADDR, error);
    }
}

void LcdDisplay::showBooting() {
    if (!isAvailable) return;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DUAL-PLC GATEWAY");
    lcd.setCursor(0, 1);
    lcd.print("Connecting WiFi.");
}

void LcdDisplay::showConnected(const String& ip) {
    if (!isAvailable) return;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("IP: ");
    lcd.print(ip);
    lcd.setCursor(0, 1);
    lcd.print("WiFi Connected  ");
}

void LcdDisplay::updateStatus(
    const String& ip,
    bool qConnected,
    bool rConnected,
    uint8_t y20State
) {
    if (!isAvailable) return;

    uint32_t now = millis();
    if (now - lastUpdateMs < 1000) return; // Refresh at most once per second
    lastUpdateMs = now;

    // Line 1: IP address (e.g. "IP:192.168.3.15 ")
    char line1[17];
    snprintf(line1, sizeof(line1), "IP:%-13s", ip.c_str());
    lcd.setCursor(0, 0);
    lcd.print(line1);

    // Line 2: "Q:OK  R:UNV Y20:0"
    const char* qStr = qConnected ? "OK " : "ERR";
    const char* rStr = rConnected ? "OK " : "UNV";
    char line2[17];
    snprintf(line2, sizeof(line2), "Q:%s R:%s Y20:%d", qStr, rStr, y20State);
    lcd.setCursor(0, 1);
    lcd.print(line2);
}
