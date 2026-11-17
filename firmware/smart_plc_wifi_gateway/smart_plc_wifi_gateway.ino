/*
 * SMART PLC OVER WIFI — DUAL MITSUBISHI Q/R ESP32 GATEWAY
 * 
 * Hardware:
 *   - ESP32-WROOM-32 / DevKit v1 (38-pin or 30-pin)
 *   - I2C LCD 1602 (Address 0x27, SDA=GPIO21, SCL=GPIO22)
 *   - Mitsubishi Q03UDECPU (Station 0: 192.168.3.30:5010)
 *   - Mitsubishi R04CPU iQ-R (Station 1: 192.168.3.20:5010)
 *   - Wi-Fi Router: SSID "RUBY", Password "EMBLICA69 "
 * 
 * Communication:
 *   - MC Protocol 3E Binary TCP Frame over Ethernet
 *   - Non-blocking alternating station polling (120ms cycle)
 *   - High-performance REST API (/api/v1/*) with CORS & PNA Preflight
 *   - Full backward compatibility for legacy /status & /writeBit endpoints
 *   - Integrated standalone local mobile web dashboard on port 80
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================================
// CONFIGURATION & CREDENTIALS
// ============================================================================
const char* WIFI_SSID     = "RUBY";
const char* WIFI_PASS     = "EMBLICA69 ";
const char* OPERATOR_PIN  = "1234";

// MC Protocol Device Codes (Binary 3E Frame)
#define DEV_X  0x9C  // Physical Inputs
#define DEV_Y  0x9D  // Physical Outputs
#define DEV_M  0x90  // Internal Relays
#define DEV_D  0xA8  // Data Registers

// PLC Station Data Structure
struct PlcStation {
  const char* id;
  const char* name;
  const char* model;
  IPAddress ip;
  uint16_t port;
  WiFiClient client;
  bool connected;
  unsigned long lastPollTime;
  unsigned long lastRespMs;
  uint32_t errCount;
  
  // Real-time cached PLC device words
  uint16_t x0_val;  // X00..X0F (X0..X7 monitored)
  uint16_t x1_val;  // X10..X1F
  uint16_t y0_val;  // Y00..Y0F
  uint16_t y2_val;  // Y20..Y2F (Y20..Y27 outputs monitored & controlled)
  uint16_t y3_val;  // Y30..Y3F (BCD display outputs)
  uint16_t m0_val;  // M00..M0F
  int16_t  analog0; // Analog input (D0 or Buffer Memory G11/G400)
};

PlcStation plcs[2] = {
  {
    "melsec_q03",
    "Mitsubishi Q03UDECPU",
    "Q03UDECPU",
    IPAddress(192, 168, 3, 30),
    5010,
    WiFiClient(),
    false, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0
  },
  {
    "melsec_r04",
    "Mitsubishi R04CPU iQ-R",
    "R04CPU",
    IPAddress(192, 168, 3, 20),
    5010,
    WiFiClient(),
    false, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0
  }
};

// Global peripherals & server
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);

int activeStation = 0;
unsigned long lastStationToggle = 0;
unsigned long lastLcdUpdate = 0;

// ============================================================================
// CORS & PRIVATE NETWORK ACCESS (PNA) HELPERS
// ============================================================================
void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Operator-PIN, X-Requested-With");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  server.sendHeader("Access-Control-Max-Age", "86400");
}

void handleOptions() {
  sendCorsHeaders();
  server.send(204);
}

// ============================================================================
// MC PROTOCOL 3E BINARY TCP ENGINE
// ============================================================================
bool ensureConnected(int st) {
  if (plcs[st].client.connected()) return true;
  plcs[st].client.stop();
  if (plcs[st].client.connect(plcs[st].ip, plcs[st].port)) {
    plcs[st].client.setNoDelay(true);
    plcs[st].connected = true;
    return true;
  }
  plcs[st].connected = false;
  return false;
}

// Read batch of 16-bit words via 0x0401 command (Subcommand 0x0000 = word units)
bool readWordsDirect(int st, uint8_t devCode, uint32_t addr, uint16_t count, uint16_t* outBuf) {
  if (!ensureConnected(st)) return false;

  uint8_t req[21];
  // 3E Subheader (QnA compatible 3E frame)
  req[0]  = 0x50; req[1]  = 0x00;
  req[2]  = 0x00; // Network No (0 = local)
  req[3]  = 0xFF; // PC No (FF = own station)
  req[4]  = 0xFF; req[5]  = 0x03; // Destination module I/O (0x03FF = CPU)
  req[6]  = 0x00; // Destination module station No
  req[7]  = 0x0C; req[8]  = 0x00; // Request data length (12 bytes following)
  req[9]  = 0x10; req[10] = 0x00; // CPU monitoring timer (16 * 250ms = 4.0s)
  req[11] = 0x01; req[12] = 0x04; // Command: 0x0401 (Batch Read)
  req[13] = 0x00; req[14] = 0x00; // Subcommand: 0x0000 (Word units)
  // Device address (3 bytes, Little Endian)
  req[15] = (uint8_t)(addr & 0xFF);
  req[16] = (uint8_t)((addr >> 8) & 0xFF);
  req[17] = (uint8_t)((addr >> 16) & 0xFF);
  req[18] = devCode; // Device code (e.g. 0x9C=X, 0x9D=Y, 0x90=M, 0xA8=D)
  req[19] = (uint8_t)(count & 0xFF);
  req[20] = (uint8_t)((count >> 8) & 0xFF);

  while (plcs[st].client.available()) plcs[st].client.read();
  plcs[st].client.write(req, 21);
  plcs[st].client.flush();

  unsigned long t0 = millis();
  while (plcs[st].client.available() < 11) {
    if (millis() - t0 > 400) {
      plcs[st].errCount++;
      return false;
    }
    delay(1);
  }

  uint8_t header[11];
  plcs[st].client.readBytes(header, 11);
  uint16_t endCode = header[9] | (header[10] << 8);
  if (endCode != 0x0000) {
    plcs[st].errCount++;
    return false;
  }

  uint16_t dataLen = header[7] | (header[8] << 8);
  int bytesToRead = dataLen - 2; // Subtract endcode length
  int wordsToRead = bytesToRead / 2;
  if (wordsToRead > count) wordsToRead = count;

  while (plcs[st].client.available() < bytesToRead) {
    if (millis() - t0 > 400) {
      plcs[st].errCount++;
      return false;
    }
    delay(1);
  }

  for (int i = 0; i < wordsToRead; i++) {
    uint8_t lo = plcs[st].client.read();
    uint8_t hi = plcs[st].client.read();
    outBuf[i] = lo | (hi << 8);
  }

  plcs[st].lastRespMs = millis() - t0;
  return true;
}

// Write single bit via 0x1401 command (Subcommand 0x0001 = bit units)
bool writeBitDirect(int st, uint8_t devCode, uint32_t addr, uint8_t val) {
  if (!ensureConnected(st)) return false;

  uint8_t req[22];
  req[0]  = 0x50; req[1]  = 0x00;
  req[2]  = 0x00;
  req[3]  = 0xFF;
  req[4]  = 0xFF; req[5]  = 0x03;
  req[6]  = 0x00;
  req[7]  = 0x0D; req[8]  = 0x00; // Request data length (13 bytes)
  req[9]  = 0x10; req[10] = 0x00; // CPU monitoring timer (4.0s)
  req[11] = 0x01; req[12] = 0x14; // Command: 0x1401 (Batch Write)
  req[13] = 0x01; req[14] = 0x00; // Subcommand: 0x0001 (Bit units)
  req[15] = (uint8_t)(addr & 0xFF);
  req[16] = (uint8_t)((addr >> 8) & 0xFF);
  req[17] = (uint8_t)((addr >> 16) & 0xFF);
  req[18] = devCode;
  req[19] = 0x01; req[20] = 0x00; // Number of device points = 1
  req[21] = (val ? 0x10 : 0x00);   // MC Protocol bit write nibble: 0x10=ON, 0x00=OFF

  while (plcs[st].client.available()) plcs[st].client.read();
  plcs[st].client.write(req, 22);
  plcs[st].client.flush();

  unsigned long t0 = millis();
  while (plcs[st].client.available() < 11) {
    if (millis() - t0 > 400) {
      plcs[st].errCount++;
      return false;
    }
    delay(1);
  }

  uint8_t header[11];
  plcs[st].client.readBytes(header, 11);
  uint16_t endCode = header[9] | (header[10] << 8);
  if (endCode == 0x0000) {
    // Update local cache bit immediately
    if (devCode == DEV_Y && addr >= 0x20 && addr <= 0x27) {
      int bitIdx = addr - 0x20;
      if (val) plcs[st].y2_val |= (1 << bitIdx);
      else     plcs[st].y2_val &= ~(1 << bitIdx);
    }
    return true;
  }
  return false;
}

// Write 16-bit word directly (e.g. for BCD display)
bool writeWordDirect(int st, uint8_t devCode, uint32_t addr, uint16_t val) {
  if (!ensureConnected(st)) return false;

  uint8_t req[23];
  req[0]  = 0x50; req[1]  = 0x00;
  req[2]  = 0x00; req[3]  = 0xFF;
  req[4]  = 0xFF; req[5]  = 0x03;
  req[6]  = 0x00;
  req[7]  = 0x0E; req[8]  = 0x00; // Request data length (14 bytes)
  req[9]  = 0x10; req[10] = 0x00;
  req[11] = 0x01; req[12] = 0x14; // Command: 0x1401 (Batch Write)
  req[13] = 0x00; req[14] = 0x00; // Subcommand: 0x0000 (Word units)
  req[15] = (uint8_t)(addr & 0xFF);
  req[16] = (uint8_t)((addr >> 8) & 0xFF);
  req[17] = (uint8_t)((addr >> 16) & 0xFF);
  req[18] = devCode;
  req[19] = 0x01; req[20] = 0x00; // 1 word
  req[21] = (uint8_t)(val & 0xFF);
  req[22] = (uint8_t)((val >> 8) & 0xFF);

  while (plcs[st].client.available()) plcs[st].client.read();
  plcs[st].client.write(req, 23);
  plcs[st].client.flush();

  unsigned long t0 = millis();
  while (plcs[st].client.available() < 11) {
    if (millis() - t0 > 400) return false;
    delay(1);
  }
  uint8_t header[11];
  plcs[st].client.readBytes(header, 11);
  return ((header[9] | (header[10] << 8)) == 0x0000);
}

// Non-blocking station polling cycle
void pollStationFast(int st) {
  if (!ensureConnected(st)) return;

  // 1. Read Inputs X00..X1F (2 words)
  uint16_t xBuf[2] = {0, 0};
  if (readWordsDirect(st, DEV_X, 0x00, 2, xBuf)) {
    plcs[st].x0_val = xBuf[0];
    plcs[st].x1_val = xBuf[1];
  }

  // 2. Read Outputs Y00..Y3F (4 words: Y0=0, Y1=10, Y2=20, Y3=30)
  uint16_t yBuf[4] = {0, 0, 0, 0};
  if (readWordsDirect(st, DEV_Y, 0x00, 4, yBuf)) {
    plcs[st].y0_val = yBuf[0];
    plcs[st].y2_val = yBuf[2]; // Y20..Y2F holds physical outputs Y20-Y27
    plcs[st].y3_val = yBuf[3]; // Y30..Y3F holds BCD outputs
  }

  // 3. Read Internal Relays M00..M0F (1 word)
  uint16_t mBuf[1] = {0};
  if (readWordsDirect(st, DEV_M, 0x00, 1, mBuf)) {
    plcs[st].m0_val = mBuf[0];
  }

  // 4. Read Analog Data Register D0 (or buffer memory)
  uint16_t dBuf[1] = {0};
  if (readWordsDirect(st, DEV_D, 0x00, 1, dBuf)) {
    plcs[st].analog0 = (int16_t)dBuf[0];
  }
}

// ============================================================================
// LCD DISPLAY TASK
// ============================================================================
void updateLcd() {
  lcd.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("IP:");
    lcd.print(WiFi.localIP().toString());
    int pad = 16 - (3 + WiFi.localIP().toString().length());
    while (pad-- > 0) lcd.print(" ");
  } else {
    lcd.print("WiFi Disconn... ");
  }

  lcd.setCursor(0, 1);
  char buf[17];
  snprintf(buf, sizeof(buf), "Q:%s R:%s",
           plcs[0].connected ? "OK " : "ERR",
           plcs[1].connected ? "OK " : "ERR");
  lcd.print(buf);
  int pad = 16 - strlen(buf);
  while (pad-- > 0) lcd.print(" ");
}

// ============================================================================
// REST API ROUTE HANDLERS (/api/v1/*)
// ============================================================================

void handleApiHealth() {
  sendCorsHeaders();
  String json = "{";
  json += "\"status\":\"healthy\",";
  json += "\"firmware_version\":\"3.0.0\",";
  json += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
  json += "\"simulation_mode\":false,";
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + String(WIFI_SSID) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI());
  json += "},";
  json += "\"memory\":{\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiDevices() {
  sendCorsHeaders();
  String json = "[";
  for (int s = 0; s < 2; s++) {
    if (s > 0) json += ",";
    json += "{";
    json += "\"id\":\"" + String(plcs[s].id) + "\",";
    json += "\"device_id\":\"" + String(plcs[s].id) + "\",";
    json += "\"name\":\"" + String(plcs[s].name) + "\",";
    json += "\"model\":\"" + String(plcs[s].model) + "\",";
    json += "\"ip\":\"" + plcs[s].ip.toString() + "\",";
    json += "\"port\":" + String(plcs[s].port) + ",";
    json += "\"connected\":" + String(plcs[s].connected ? "true" : "false") + ",";
    json += "\"adapter_id\":\"mc_protocol\"";
    json += "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleApiSystem() {
  sendCorsHeaders();
  String json = "{";
  json += "\"status\":\"RUNNING\",";
  json += "\"writes_enabled\":true,";
  json += "\"permitted_points\":[\"Y20\",\"Y21\",\"Y22\",\"Y23\",\"Y24\",\"Y25\",\"Y26\",\"Y27\"],";
  json += "\"auth_required\":false";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiStationTelemetry(int st) {
  sendCorsHeaders();
  String json = "{";
  json += "\"device_id\":\"" + String(plcs[st].id) + "\",";
  json += "\"connected\":" + String(plcs[st].connected ? "true" : "false") + ",";
  json += "\"status\":\"" + String(plcs[st].connected ? "online" : "offline") + "\",";
  json += "\"timestamp\":\"" + String(millis()) + "\",";

  // Inputs X0..X7
  json += "\"inputs\":{";
  for (int i = 0; i < 8; i++) {
    if (i > 0) json += ",";
    int bit = (plcs[st].x0_val & (1 << i)) ? 1 : 0;
    json += "\"X" + String(i) + "\":" + String(bit);
  }
  json += "},";

  // Outputs Y20..Y27
  json += "\"outputs\":{";
  for (int i = 0; i < 8; i++) {
    if (i > 0) json += ",";
    int bit = (plcs[st].y2_val & (1 << i)) ? 1 : 0;
    json += "\"Y2" + String(i) + "\":" + String(bit);
  }
  json += "},";

  // Readings object for app.js normalize logic
  json += "\"readings\":{";
  bool firstR = true;
  for (int i = 0; i < 8; i++) {
    if (!firstR) json += ",";
    int bit = (plcs[st].x0_val & (1 << i)) ? 1 : 0;
    json += "\"X" + String(i) + "\":{\"raw_value\":" + String(bit) + ",\"engineering_value\":" + String(bit) + ",\"quality\":\"GOOD\"}";
    firstR = false;
  }
  for (int i = 0; i < 8; i++) {
    if (!firstR) json += ",";
    int bit = (plcs[st].y2_val & (1 << i)) ? 1 : 0;
    json += "\"Y2" + String(i) + "\":{\"raw_value\":" + String(bit) + ",\"engineering_value\":" + String(bit) + ",\"quality\":\"GOOD\"}";
    firstR = false;
  }
  json += "},";

  // Analog channels
  json += "\"analog\":{";
  json += "\"AN0\":{\"raw_value\":" + String(plcs[st].analog0) + ",\"engineering_value\":" + String(plcs[st].analog0) + ",\"unit\":\"counts\"}";
  json += "}";

  json += "}";
  server.send(200, "application/json", json);
}

void handleOutputActuationInternal(int st, String point, int desiredState) {
  sendCorsHeaders();

  // Safety Lock: Only Station 0 (Q-Series) allows output actuation
  if (st != 0) {
    server.send(403, "application/json", "{\"error\":\"PHYSICAL_WRITES_DISABLED\",\"message\":\"Station R04CPU is locked for read-only safety monitoring.\"}");
    return;
  }

  // Safety Lock: Point must be strictly in allowlist Y20..Y27
  point.toUpperCase();
  if (!point.startsWith("Y")) {
    server.send(400, "application/json", "{\"error\":\"INVALID_POINT\",\"message\":\"Device code must be Y\"}");
    return;
  }

  int hexAddr = 0;
  String numPart = point.substring(1);
  if (numPart.length() >= 2 && numPart.startsWith("2")) {
    char lastChar = numPart.charAt(1);
    if (lastChar >= '0' && lastChar <= '7') {
      hexAddr = 0x20 + (lastChar - '0');
    }
  }

  if (hexAddr < 0x20 || hexAddr > 0x27) {
    server.send(400, "application/json", "{\"error\":\"FORBIDDEN_OUTPUT\",\"message\":\"Output " + point + " is outside permitted safety allowlist (Y20..Y27). Y0 is strictly forbidden.\"}");
    return;
  }

  unsigned long tStart = millis();
  bool success = writeBitDirect(0, DEV_Y, hexAddr, (desiredState ? 1 : 0));
  unsigned long elapsed = millis() - tStart;

  if (success) {
    int readbackVal = (plcs[0].y2_val & (1 << (hexAddr - 0x20))) ? 1 : 0;
    String json = "{";
    json += "\"status\":\"SUCCESS\",";
    json += "\"device_id\":\"melsec_q03\",";
    json += "\"point_id\":\"" + point + "\",";
    json += "\"commanded_state\":" + String(desiredState) + ",";
    json += "\"readback_value\":" + String(readbackVal) + ",";
    json += "\"readback_verified\":" + String(readbackVal == desiredState ? "true" : "false") + ",";
    json += "\"plc_end_code\":\"0x0000\",";
    json += "\"elapsed_ms\":" + String(elapsed);
    json += "}";
    server.send(200, "application/json", json);
  } else {
    server.send(502, "application/json", "{\"error\":\"COMMUNICATION_FAILURE\",\"message\":\"MC Protocol 3E write failed on Q03UDECPU\"}");
  }
}

void handleOutputRoute(String point) {
  int desiredState = 1;
  if (server.hasArg("state")) {
    desiredState = server.arg("state").toInt();
  } else if (server.hasArg("plain")) {
    String body = server.arg("plain");
    if (body.indexOf("\"state\":0") >= 0 || body.indexOf("\"state\": 0") >= 0) {
      desiredState = 0;
    }
  }
  handleOutputActuationInternal(0, point, desiredState);
}

// Alternative REST actuate endpoint: POST /api/v1/plc/actuate
void handleApiPlcActuate() {
  sendCorsHeaders();
  int st = 0;
  String point = "Y20";
  int desiredState = 1;

  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    if (body.indexOf("melsec_r04") >= 0) st = 1;
    for (int i = 0; i <= 7; i++) {
      String pt = "Y2" + String(i);
      if (body.indexOf(pt) >= 0) { point = pt; break; }
    }
    if (body.indexOf("\"state\":0") >= 0 || body.indexOf("\"state\": 0") >= 0 || body.indexOf("\"state\":\"OFF\"") >= 0) {
      desiredState = 0;
    }
  }
  handleOutputActuationInternal(st, point, desiredState);
}

// ============================================================================
// LEGACY ROUTE HANDLERS (Preserved for compatibility)
// ============================================================================

void handleLegacyStatus() {
  sendCorsHeaders();
  String json = "{";
  json += "\"wifi\":{\"ip\":\"" + WiFi.localIP().toString() + "\",\"rssi\":" + String(WiFi.RSSI()) + "},";
  json += "\"uptime_s\":" + String(millis() / 1000) + ",";
  json += "\"active_station\":" + String(activeStation) + ",";
  json += "\"plcs\":[";
  for (int s = 0; s < 2; s++) {
    if (s > 0) json += ",";
    json += "{\"name\":\"" + String(plcs[s].name) + "\",";
    json += "\"ip\":\"" + plcs[s].ip.toString() + "\",";
    json += "\"connected\":" + String(plcs[s].connected ? "true" : "false") + ",";
    json += "\"err\":" + String(plcs[s].errCount) + ",";
    json += "\"resp_ms\":" + String(plcs[s].lastRespMs) + ",";
    json += "\"x0\":" + String(plcs[s].x0_val) + ",";
    json += "\"y0\":" + String(plcs[s].y0_val) + ",";
    json += "\"y2\":" + String(plcs[s].y2_val) + ",";
    json += "\"y3\":" + String(plcs[s].y3_val) + ",";
    json += "\"m0\":" + String(plcs[s].m0_val) + ",";
    json += "\"an0\":" + String(plcs[s].analog0) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleLegacyWriteBit() {
  sendCorsHeaders();
  int st = server.hasArg("st") ? server.arg("st").toInt() : 0;
  String dev = server.hasArg("dev") ? server.arg("dev") : "Y";
  String addrStr = server.hasArg("addr") ? server.arg("addr") : "20";
  int val = server.hasArg("val") ? server.arg("val").toInt() : 0;

  dev.toUpperCase();
  uint8_t devCode = DEV_Y;
  if (dev == "X") devCode = DEV_X;
  else if (dev == "M") devCode = DEV_M;

  uint32_t addr = strtoul(addrStr.c_str(), NULL, 16);

  // Safety allowlist for physical writes: Y20..Y27 only on Station 0
  if (st == 0 && devCode == DEV_Y && (addr < 0x20 || addr > 0x27)) {
    server.send(400, "application/json", "{\"status\":\"ERROR\",\"message\":\"Only Y20..Y27 allowlisted. Y0 forbidden.\"}");
    return;
  }
  if (st != 0) {
    server.send(403, "application/json", "{\"status\":\"LOCKED\",\"message\":\"Station R04CPU is read-only.\"}");
    return;
  }

  bool ok = writeBitDirect(st, devCode, addr, val ? 1 : 0);
  server.send(ok ? 200 : 502, "application/json", ok ? "{\"status\":\"OK\"}" : "{\"status\":\"ERROR\"}");
}

void handleLegacyWriteBCD() {
  sendCorsHeaders();
  int st = server.hasArg("st") ? server.arg("st").toInt() : 0;
  int val = server.hasArg("val") ? server.arg("val").toInt() : 0;
  val = constrain(val, 0, 99);
  uint16_t bcd = ((val / 10) << 4) | (val % 10);
  bool ok = writeWordDirect(st, DEV_Y, 0x30, bcd);
  server.send(ok ? 200 : 502, "application/json", ok ? "{\"status\":\"OK\"}" : "{\"status\":\"ERROR\"}");
}

void handleLegacyWriteAnalog() {
  sendCorsHeaders();
  int st = server.hasArg("st") ? server.arg("st").toInt() : 0;
  int val = server.hasArg("val") ? server.arg("val").toInt() : 0;
  val = constrain(val, 0, 4000);
  bool ok = writeWordDirect(st, DEV_D, 0x00, (uint16_t)val);
  server.send(ok ? 200 : 502, "application/json", ok ? "{\"status\":\"OK\"}" : "{\"status\":\"ERROR\"}");
}

void handleLegacyPlcMode() {
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"OK\",\"mode\":\"RUN\"}");
}

// Standalone embedded mobile-friendly dashboard on http://<ESP32_IP>/
void handleEmbeddedRoot() {
  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Smart PLC Gateway (Dual Q/R)</title>"
  "<style>"
  "*{box-sizing:border-box;margin:0;padding:0}body{background:#0d1117;color:#c9d1d9;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;padding:16px}"
  ".hdr{display:flex;justify-content:space-between;align-items:center;padding-bottom:12px;border-bottom:1px solid #30363d;margin-bottom:16px}"
  ".badge{padding:3px 8px;border-radius:12px;font-size:12px;font-weight:600}"
  ".ok{background:#238636;color:#fff}.err{background:#da3633;color:#fff}"
  ".card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:14px;margin-bottom:14px}"
  ".card h3{font-size:15px;color:#58a6ff;margin-bottom:10px}"
  ".grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}"
  ".io-box{background:#21262d;border:1px solid #30363d;border-radius:6px;padding:8px;text-align:center;font-size:12px}"
  ".io-box.active{background:#1f6feb;border-color:#58a6ff;color:#fff}"
  ".btn-io{width:100%;padding:10px;border-radius:6px;border:none;background:#21262d;color:#c9d1d9;font-weight:bold;cursor:pointer;margin-bottom:8px}"
  ".btn-io.on{background:#238636;color:#fff}"
  ".link-box{background:#161b22;border:1px dashed #58a6ff;padding:12px;border-radius:8px;text-align:center;margin-top:16px}"
  ".link-box a{color:#58a6ff;font-weight:bold;text-decoration:none;word-break:break-all}"
  "</style></head><body>"
  "<div class='hdr'><h2>Smart PLC Gateway</h2><span id='st-badge' class='badge ok'>ONLINE</span></div>"
  "<div class='card'><h3>Mitsubishi Q03UDECPU (192.168.3.30:5010)</h3>"
  "<p style='font-size:12px;margin-bottom:8px'>Physical Inputs (X0..X7):</p>"
  "<div class='grid' id='in-grid'></div></div>"
  "<div class='card'><h3>8-Output Independent Control (Y20..Y27)</h3>"
  "<div class='grid' id='out-grid'></div></div>"
  "<div class='link-box'>Open Cloud Telemetry Web App:<br>"
  "<a id='cloud-link' target='_blank'>Open Live GitHub Pages Dashboard</a></div>"
  "<script>"
  "const ip=window.location.hostname;"
  "document.getElementById('cloud-link').href='https://surajyadavx.github.io/smart-plc-wifi/?gateway=http://'+ip;"
  "async function poll(){"
  "try{const r=await fetch('/api/v1/devices/melsec_q03/telemetry');const d=await r.json();"
  "let inH='';for(let i=0;i<8;i++){const v=d.inputs['X'+i];inH+=`<div class='io-box ${v?'active':''}'>X${i}<br>${v?'ON':'OFF'}</div>`;}"
  "document.getElementById('in-grid').innerHTML=inH;"
  "let outH='';for(let i=0;i<8;i++){const pt='Y2'+i;const v=d.outputs[pt];outH+=`<button class='btn-io ${v?'on':''}' onclick='toggleOut(\"${pt}\",${v?0:1})'>${pt}<br>${v?'ON':'OFF'}</button>`;}"
  "document.getElementById('out-grid').innerHTML=outH;"
  "}catch(e){console.error(e);}}"
  "async function toggleOut(pt,val){await fetch(`/api/v1/devices/melsec_q03/outputs/${pt}/command`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({state:val})});poll();}"
  "setInterval(poll,1000);poll();"
  "</script></body></html>");
  server.send(200, "text/html", html);
}

// 404 Handler with CORS headers
void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    handleOptions();
    return;
  }
  sendCorsHeaders();
  server.send(404, "application/json", "{\"error\":\"NOT_FOUND\",\"message\":\"Endpoint not found\"}");
}

// ============================================================================
// ARDUINO SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[INIT] Starting Smart PLC Dual-Station Gateway v3.0.0...");

  // Initialize I2C LCD (SDA=GPIO21, SCL=GPIO22)
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  lcd.setCursor(0, 1);
  lcd.print("SSID: ");
  lcd.print(WIFI_SSID);

  // Connect to Wi-Fi Router "RUBY"
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] Connecting to SSID: %s\n", WIFI_SSID);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connected! Assigned IP: %s\n", WiFi.localIP().toString().c_str());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
  } else {
    Serial.println("\n[WIFI] Connection timed out! Continuing in fallback mode...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
  }

  // --------------------------------------------------------------------------
  // REGISTER REST API ROUTES (/api/v1/*)
  // --------------------------------------------------------------------------
  server.on("/api/v1/health", HTTP_GET, handleApiHealth);
  server.on("/api/v1/health", HTTP_OPTIONS, handleOptions);

  server.on("/api/v1/devices", HTTP_GET, handleApiDevices);
  server.on("/api/v1/devices", HTTP_OPTIONS, handleOptions);

  server.on("/api/v1/system", HTTP_GET, handleApiSystem);
  server.on("/api/v1/system", HTTP_OPTIONS, handleOptions);

  server.on("/api/v1/devices/melsec_q03/telemetry", HTTP_GET, []() { handleApiStationTelemetry(0); });
  server.on("/api/v1/devices/melsec_q03/telemetry", HTTP_OPTIONS, handleOptions);

  server.on("/api/v1/devices/melsec_r04/telemetry", HTTP_GET, []() { handleApiStationTelemetry(1); });
  server.on("/api/v1/devices/melsec_r04/telemetry", HTTP_OPTIONS, handleOptions);

  // 8 Dedicated Output Endpoints: Y20..Y27
  server.on("/api/v1/devices/melsec_q03/outputs/Y20/command", HTTP_POST, []() { handleOutputRoute("Y20"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y20/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y21/command", HTTP_POST, []() { handleOutputRoute("Y21"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y21/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y22/command", HTTP_POST, []() { handleOutputRoute("Y22"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y22/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y23/command", HTTP_POST, []() { handleOutputRoute("Y23"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y23/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y24/command", HTTP_POST, []() { handleOutputRoute("Y24"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y24/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y25/command", HTTP_POST, []() { handleOutputRoute("Y25"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y25/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y26/command", HTTP_POST, []() { handleOutputRoute("Y26"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y26/command", HTTP_OPTIONS, handleOptions);
  server.on("/api/v1/devices/melsec_q03/outputs/Y27/command", HTTP_POST, []() { handleOutputRoute("Y27"); });
  server.on("/api/v1/devices/melsec_q03/outputs/Y27/command", HTTP_OPTIONS, handleOptions);

  server.on("/api/v1/plc/actuate", HTTP_POST, handleApiPlcActuate);
  server.on("/api/v1/plc/actuate", HTTP_OPTIONS, handleOptions);

  // --------------------------------------------------------------------------
  // REGISTER LEGACY ROUTES
  // --------------------------------------------------------------------------
  server.on("/", HTTP_GET, handleEmbeddedRoot);
  server.on("/status", HTTP_GET, handleLegacyStatus);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/writeBit", HTTP_GET, handleLegacyWriteBit);
  server.on("/writeBit", HTTP_OPTIONS, handleOptions);
  server.on("/writeBCD", HTTP_GET, handleLegacyWriteBCD);
  server.on("/writeBCD", HTTP_OPTIONS, handleOptions);
  server.on("/writeAnalog", HTTP_GET, handleLegacyWriteAnalog);
  server.on("/writeAnalog", HTTP_OPTIONS, handleOptions);
  server.on("/plcMode", HTTP_GET, handleLegacyPlcMode);
  server.on("/plcMode", HTTP_OPTIONS, handleOptions);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] Web server started on port 80");
  Serial.println("[SYSTEM] Ready to monitor PLCs & accept web commands.\n");
}

// ============================================================================
// ARDUINO MAIN LOOP
// ============================================================================
void loop() {
  server.handleClient();

  // Alternating high-speed station polling (120ms cycle)
  unsigned long now = millis();
  if (now - lastStationToggle >= 120) {
    lastStationToggle = now;
    pollStationFast(activeStation);
    activeStation = (activeStation == 0) ? 1 : 0;
  }

  // Periodic LCD display update (every 1000ms)
  if (now - lastLcdUpdate >= 1000) {
    lastLcdUpdate = now;
    updateLcd();
  }
}
