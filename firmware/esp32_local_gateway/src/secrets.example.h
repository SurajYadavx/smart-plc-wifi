#pragma once

/**
 * secrets.example.h
 * ──────────────────
 * Template for Wi-Fi and Operator Credentials.
 * 
 * INSTRUCTIONS:
 * 1. Copy this file to 'secrets.h' in the same directory:
 *      cp secrets.example.h secrets.h
 * 2. Edit secrets.h with your actual local router credentials.
 * 3. NEVER commit secrets.h to Git! (It is protected by .gitignore).
 */

// Local Network (TP-Link RUBY) Wi-Fi Configuration
#define SECRET_WIFI_SSID     "RUBY"
#define SECRET_WIFI_PASSWORD "YOUR_WIFI_PASSWORD_HERE"

// Operator Authorization PIN for PLC Output Actuation
// SAFETY MANDATE (V2.1/V3): Must be set to a private PIN. Empty or placeholder PINs fail closed!
// An empty PIN ("") guarantees that unconfigured systems fail closed safely.
#define SECRET_OPERATOR_PIN  ""
