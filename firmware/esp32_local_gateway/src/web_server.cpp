#include "web_server.h"
#include <WiFi.h>
#include "lcd_display.h"

GatewayWebServer gatewayWebServer;

GatewayWebServer::GatewayWebServer() : server(80) {}

void GatewayWebServer::begin() {
    Serial.println(F("[HTTP] Initializing LittleFS filesystem (non-destructive)..."));
    // Section 13: Avoid automatic formatting on mount failure to protect stored assets
    if (LittleFS.begin(false)) {
        fsMounted = true;
        Serial.printf("[HTTP] LittleFS mounted successfully. Total: %u KB, Used: %u KB\n",
            (unsigned int)(LittleFS.totalBytes() / 1024),
            (unsigned int)(LittleFS.usedBytes() / 1024)
        );
    } else {
        fsMounted = false;
        Serial.println(F("[HTTP] ERROR: LittleFS mount failed — auto-formatting disabled to prevent asset loss."));
        Serial.println(F("[HTTP] Upload filesystem image using: pio run -t uploadfs"));
    }

    registerRoutes();
    server.begin();
    Serial.println(F("[HTTP] Local Web Server listening on port 80"));
}

void GatewayWebServer::loop() {
    server.handleClient();
}

void GatewayWebServer::sendCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Operator-PIN, X-Agent-API-Key, X-Requested-With, Access-Control-Request-Private-Network");
    server.sendHeader("Access-Control-Allow-Private-Network", "true");
    server.sendHeader("Access-Control-Max-Age", "86400");
}

void GatewayWebServer::handleCorsPreflight() {
    sendCorsHeaders();
    server.send(204);
}

void GatewayWebServer::registerRoutes() {
    // ── Preflight OPTIONS Handlers for CORS ────────────────────────────────────
    server.onNotFound([this]() {
        if (server.method() == HTTP_OPTIONS) {
            handleCorsPreflight();
            return;
        }

        // Try serving from LittleFS
        String path = server.uri();
        if (handleFileRead(path)) return;

        // Fallback for root / index.html
        if (path == "/" || path == "/index.html") {
            serveFallbackIndex();
            return;
        }

        server.send(404, "application/json", "{\"error\":true,\"message\":\"Not Found\"}");
    });

    // ── REST API Endpoints ────────────────────────────────────────────────────
    
    // GET /api/v1/system and GET /api/v1/health (Canonical System Health)
    server.on("/api/v1/system", HTTP_GET, [this]() {
        handleApiSystem();
    });
    server.on("/api/v1/health", HTTP_GET, [this]() {
        handleApiSystem();
    });

    // GET /api/v1/devices
    server.on("/api/v1/devices", HTTP_GET, [this]() {
        handleApiDevices();
    });

    // Station 1: Q-Series Detail, Telemetry, Diagnostics
    server.on("/api/v1/devices/" Q_PLC_ID, HTTP_GET, [this]() {
        handleApiDeviceDetail(Q_PLC_ID);
    });
    server.on("/api/v1/devices/" Q_PLC_ID "/telemetry", HTTP_GET, [this]() {
        handleApiTelemetry(Q_PLC_ID);
    });
    server.on("/api/v1/devices/" Q_PLC_ID "/diagnostics", HTTP_GET, [this]() {
        handleApiDiagnostics(Q_PLC_ID);
    });

    // Station 2: R-Series Detail, Telemetry, Diagnostics
    server.on("/api/v1/devices/" R_PLC_ID, HTTP_GET, [this]() {
        handleApiDeviceDetail(R_PLC_ID);
    });
    server.on("/api/v1/devices/" R_PLC_ID "/telemetry", HTTP_GET, [this]() {
        handleApiTelemetry(R_PLC_ID);
    });
    server.on("/api/v1/devices/" R_PLC_ID "/diagnostics", HTTP_GET, [this]() {
        handleApiDiagnostics(R_PLC_ID);
    });

    // Register 8 Authorized Output Command Endpoints for Station 1 & Station 2 (Y20 - Y27)
    // Point Y0 is strictly excluded: zero Y0 route registered anywhere on server.
    for (int i = 0; i < 8; i++) {
        String pt = Q_PERMITTED_OUTPUT_NAMES[i];
        server.on(String("/api/v1/devices/") + Q_PLC_ID + "/outputs/" + pt + "/command", HTTP_POST, [this, pt]() {
            handleApiOutputCommand(Q_PLC_ID, pt);
        });
        server.on(String("/api/v1/devices/") + R_PLC_ID + "/outputs/" + pt + "/command", HTTP_POST, [this, pt]() {
            handleApiOutputCommand(R_PLC_ID, pt);
        });
    }
}

// ── Static Asset Delivery ─────────────────────────────────────────────────────

String GatewayWebServer::getContentType(const String& filename) {
    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css"))  return "text/css";
    if (filename.endsWith(".js"))   return "application/javascript";
    if (filename.endsWith(".json")) return "application/json";
    if (filename.endsWith(".ico"))  return "image/x-icon";
    if (filename.endsWith(".svg"))  return "image/svg+xml";
    if (filename.endsWith(".png"))  return "image/png";
    return "text/plain";
}

bool GatewayWebServer::handleFileRead(String path) {
    if (!fsMounted) return false;
    if (path.endsWith("/")) path += "index.html";

    String contentType = getContentType(path);

    // Check gzip compressed version first
    String pathWithGz = path + ".gz";
    if (LittleFS.exists(pathWithGz)) {
        File file = LittleFS.open(pathWithGz, "r");
        server.sendHeader("Content-Encoding", "gzip");
        server.sendHeader("Cache-Control", "public, max-age=3600");
        sendCorsHeaders();
        server.streamFile(file, contentType);
        file.close();
        return true;
    }

    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server.sendHeader("Cache-Control", "public, max-age=3600");
        sendCorsHeaders();
        server.streamFile(file, contentType);
        file.close();
        return true;
    }

    return false;
}

void GatewayWebServer::serveFallbackIndex() {
    sendCorsHeaders();
    String html = F(
        "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Q/R Dual-PLC Supervisory Gateway</title>"
        "<style>body{font-family:sans-serif;background:#0d1117;color:#c9d1d9;padding:2rem;margin:0}"
        ".card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:1.5rem;max-width:600px;margin:2rem auto}"
        "h1{color:#58a6ff;font-size:1.3rem}p{line-height:1.5;color:#8b949e}code{background:#21262d;padding:2px 6px;border-radius:4px;color:#f0883e}"
        ".status{color:#3fb950;font-weight:bold}</style></head><body>"
        "<div class='card'><h1>Q/R Dual-PLC Embedded Gateway</h1>"
        "<p>System Version: <strong>2.2.0</strong> | Status: <span class='status'>Online</span></p>"
        "<p>LittleFS static filesystem is not mounted. To deploy the complete HMI dashboard:</p>"
        "<p><code>pio run -t uploadfs</code></p>"
        "<p>The RESTful API is operational. Try: <a href='/api/v1/system' style='color:#58a6ff'>/api/v1/system</a></p>"
        "</div></body></html>"
    );
    server.send(200, "text/html", html);
}

// ── REST API Handlers ─────────────────────────────────────────────────────────

void GatewayWebServer::handleApiSystem() {
    sendCorsHeaders();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    bool qOk = plcManager.isQConnected();
    bool rOk = plcManager.isRConnected();

    JsonDocument doc;
    doc["gateway_id"] = GATEWAY_ID;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["status"] = (WiFi.status() == WL_CONNECTED) ? "healthy" : "degraded";
    doc["uptime_seconds"] = millis() / 1000;
    doc["free_heap_bytes"] = ESP.getFreeHeap();
    doc["simulation_mode"] = false;
    doc["physical_writes_enabled"] = (bool)(Q_PLC_WRITES_ENABLED || R_PLC_WRITES_ENABLED);

    // Hardware Architecture & Runtime Metrics (V2.2)
    JsonObject hw = doc["hardware"].to<JsonObject>();
    hw["chip_model"] = ESP.getChipModel();
    hw["chip_revision"] = ESP.getChipRevision();
    hw["cpu_cores"] = ESP.getChipCores();
    hw["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    hw["flash_size_bytes"] = ESP.getFlashChipSize();
    hw["flash_speed_hz"] = ESP.getFlashChipSpeed();

    // Memory Diagnostics (V2.2)
    JsonObject mem = doc["memory"].to<JsonObject>();
    mem["free_heap_bytes"] = ESP.getFreeHeap();
    mem["min_free_heap_bytes"] = ESP.getMinFreeHeap();
    mem["max_alloc_heap_bytes"] = ESP.getMaxAllocHeap();
    mem["psram_size_bytes"] = ESP.getPsramSize();
    mem["free_psram_bytes"] = ESP.getFreePsram();

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = (WiFi.status() == WL_CONNECTED);
    wifi["ssid"] = WiFi.SSID();
    wifi["ip"] = WiFi.localIP().toString();
    wifi["mac"] = WiFi.macAddress();
    wifi["rssi_dbm"] = WiFi.RSSI();

    JsonObject devices = doc["devices"].to<JsonObject>();
    devices["configured_count"] = 2;
    devices["active_count"] = (qOk ? 1 : 0) + (rOk ? 1 : 0);

    // FreeRTOS Worker Task Introspection (V2.2.1)
    JsonObject worker = doc["worker"].to<JsonObject>();
    worker["core"] = 0;
    worker["status"] = "RUNNING";
    worker["min_free_stack_bytes"] = plcManager.getWorkerMinFreeStack();

    // Actual I2C LCD Detection State (V2.2.1)
    JsonObject lcd = doc["lcd"].to<JsonObject>();
    bool lcdOk = lcdDisplay.isPresent();
    lcd["present"] = lcdOk;
    lcd["status"] = lcdOk ? "ACTIVE" : "NOT_DETECTED";

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiDevices() {
    sendCorsHeaders();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    // Station 1: Q-Series
    JsonObject q = arr.add<JsonObject>();
    DeviceTelemetry qDev = plcManager.getQTelemetry();
    q["id"] = qDev.deviceId;
    q["name"] = qDev.name;
    q["model"] = qDev.model;
    q["enabled"] = qDev.enabled;
    q["host"] = qDev.ip;
    q["port"] = qDev.port;
    q["protocol"] = "MC_3E_BINARY";
    q["connected"] = qDev.connected;
    q["status"] = qDev.connected ? "ONLINE" : "OFFLINE";
    q["data_quality"] = qDev.dataQuality;
    q["configuration_verified"] = true;
    q["physical_writes_enabled"] = Q_PLC_WRITES_ENABLED;
    JsonArray qOuts = q["permitted_outputs"].to<JsonArray>();
    if (Q_PLC_WRITES_ENABLED) {
        for (int i = 0; i < 8; i++) {
            qOuts.add(Q_PERMITTED_OUTPUT_NAMES[i]);
        }
    }

    // Station 2: R-Series
    JsonObject r = arr.add<JsonObject>();
    DeviceTelemetry rDev = plcManager.getRTelemetry();
    r["id"] = rDev.deviceId;
    r["name"] = rDev.name;
    r["model"] = rDev.model;
    r["enabled"] = rDev.enabled;
    r["host"] = rDev.ip;
    r["port"] = rDev.port;
    r["protocol"] = "SLMP_3E_BINARY";
    r["connected"] = rDev.connected;
    // Section 7: An unverified R-Series configuration must never be displayed as physically verified
    r["status"] = rDev.connected ? "UNVERIFIED" : "OFFLINE";
    r["data_quality"] = rDev.dataQuality;
    r["configuration_verified"] = false;
    r["physical_writes_enabled"] = R_PLC_WRITES_ENABLED;
    JsonArray rOuts = r["permitted_outputs"].to<JsonArray>();
    if (R_PLC_WRITES_ENABLED) {
        for (int i = 0; i < 8; i++) {
            rOuts.add(Q_PERMITTED_OUTPUT_NAMES[i]);
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiDeviceDetail(const String& deviceId) {
    sendCorsHeaders();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    if (deviceId != Q_PLC_ID && deviceId != R_PLC_ID) {
        server.send(404, "application/json", "{\"error\":true,\"message\":\"Device not found\"}");
        return;
    }

    DeviceTelemetry dev = (deviceId == Q_PLC_ID) ? plcManager.getQTelemetry() : plcManager.getRTelemetry();

    JsonDocument doc;
    doc["id"] = dev.deviceId;
    doc["name"] = dev.name;
    doc["model"] = dev.model;
    doc["enabled"] = dev.enabled;
    doc["host"] = dev.ip;
    doc["port"] = dev.port;
    doc["connected"] = dev.connected;
    doc["data_quality"] = dev.dataQuality;
    doc["configuration_verified"] = dev.configurationVerified;
    doc["physical_writes_enabled"] = (deviceId == Q_PLC_ID) ? Q_PLC_WRITES_ENABLED : R_PLC_WRITES_ENABLED;

    JsonArray outs = doc["permitted_outputs"].to<JsonArray>();
    if ((deviceId == Q_PLC_ID && Q_PLC_WRITES_ENABLED) || (deviceId == R_PLC_ID && R_PLC_WRITES_ENABLED)) {
        for (int i = 0; i < 8; i++) {
            outs.add(Q_PERMITTED_OUTPUT_NAMES[i]);
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiTelemetry(const String& deviceId) {
    sendCorsHeaders();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    if (deviceId != Q_PLC_ID && deviceId != R_PLC_ID) {
        server.send(404, "application/json", "{\"error\":true,\"message\":\"Device not found\"}");
        return;
    }

    DeviceTelemetry dev = (deviceId == Q_PLC_ID) ? plcManager.getQTelemetry() : plcManager.getRTelemetry();

    JsonDocument doc;
    doc["device_id"] = dev.deviceId;
    doc["connected"] = dev.connected;
    doc["data_quality"] = dev.dataQuality;
    doc["has_valid_sample"] = dev.hasValidSample;

    if (dev.hasValidSample) {
        doc["sample_age_ms"] = millis() - dev.diag.lastSuccessMonoMs;
    } else {
        doc["sample_age_ms"] = nullptr;
    }

    JsonObject readings = doc["readings"].to<JsonObject>();

    // CRITICAL: Valid-Zero Invariant!
    // If dev is connected and has valid sample: raw_value is 0 or 1, quality is GOOD
    // If dev is disconnected / comm failure: raw_value is null, quality is COMM_FAILURE
    for (int i = 0; i < 8; i++) {
        String xKey = "X" + String(i);
        JsonObject xPt = readings[xKey].to<JsonObject>();
        xPt["point_id"] = xKey;
        xPt["address"] = xKey;
        xPt["data_type"] = "BOOLEAN";

        if (dev.connected && dev.hasValidSample && dev.dataQuality == "GOOD") {
            xPt["raw_value"] = (int)dev.inputs[i];
            xPt["quality"] = "GOOD";
            xPt["error"] = nullptr;
        } else {
            xPt["raw_value"] = nullptr;
            xPt["quality"] = "COMM_FAILURE";
            xPt["error"] = dev.diag.lastError.length() > 0 ? dev.diag.lastError : "PLC Communication Failure";
        }
    }

    for (int i = 0; i < 8; i++) {
        String yKey = "Y" + String(0x20 + i, HEX);
        yKey.toUpperCase();
        JsonObject yPt = readings[yKey].to<JsonObject>();
        yPt["point_id"] = yKey;
        yPt["address"] = yKey;
        yPt["data_type"] = "BOOLEAN";

        if (dev.connected && dev.hasValidSample && dev.dataQuality == "GOOD") {
            yPt["raw_value"] = (int)dev.outputs[i];
            yPt["quality"] = "GOOD";
            yPt["error"] = nullptr;
        } else {
            yPt["raw_value"] = nullptr;
            yPt["quality"] = "COMM_FAILURE";
            yPt["error"] = dev.diag.lastError.length() > 0 ? dev.diag.lastError : "PLC Communication Failure";
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiDiagnostics(const String& deviceId) {
    sendCorsHeaders();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    if (deviceId != Q_PLC_ID && deviceId != R_PLC_ID) {
        server.send(404, "application/json", "{\"error\":true,\"message\":\"Device not found\"}");
        return;
    }

    DeviceTelemetry dev = (deviceId == Q_PLC_ID) ? plcManager.getQTelemetry() : plcManager.getRTelemetry();

    JsonDocument doc;
    doc["device_id"] = dev.deviceId;
    doc["connected"] = dev.connected;
    doc["packets_sent"] = dev.diag.packetsSent;
    doc["packets_received"] = dev.diag.packetsReceived;
    doc["timeouts"] = dev.diag.timeouts;
    doc["proto_errors"] = dev.diag.protoErrors;
    doc["last_latency_ms"] = dev.diag.lastLatencyMs;
    if (dev.diag.lastError.length() > 0) {
        doc["last_error"] = dev.diag.lastError;
    } else {
        doc["last_error"] = nullptr;
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiOutputCommand(const String& deviceId, const String& pointId) {
    sendCorsHeaders();
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    // ── Check Bounded Rate Limiting on Failed PIN Attempts ────────────────────
    uint32_t now = millis();
    if (lockoutUntilMs > 0 && now < lockoutUntilMs) {
        uint32_t remainSec = (lockoutUntilMs - now) / 1000 + 1;
        JsonDocument lockResp;
        lockResp["success"] = false;
        lockResp["device_id"] = deviceId;
        lockResp["point"] = pointId;
        lockResp["requested_state"] = nullptr;
        lockResp["plc_end_code"] = nullptr;
        lockResp["readback_value"] = nullptr;
        lockResp["readback_verified"] = false;
        lockResp["status"] = "TEMPORARY_LOCKOUT";
        lockResp["error"] = "Too many failed authentication attempts. Locked out for " + String(remainSec) + " seconds.";
        String respJson;
        serializeJson(lockResp, respJson);
        server.send(429, "application/json", respJson);
        return;
    }

    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_JSON\",\"error\":\"Malformed JSON payload\"}");
        return;
    }

    // ── Section 5: Strict Output State Validation BEFORE Casting ──────────────
    if (doc["state"].isNull()) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"MISSING_STATE\",\"error\":\"Field 'state' is required (numeric integer 0 or 1)\"}");
        return;
    }

    // Explicit rejection of boolean values (true / false) when integer 0/1 is required
    if (doc["state"].is<bool>()) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_STATE\",\"error\":\"Boolean values not accepted; numeric integer 0 (OFF) or 1 (ON) required\"}");
        return;
    }

    // Explicit rejection of string representations (e.g. \"1\", \"0\", \"ON\")
    if (doc["state"].is<const char*>()) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_STATE\",\"error\":\"String values not accepted; numeric integer 0 (OFF) or 1 (ON) required\"}");
        return;
    }

    // Reject non-integers, arrays, and objects
    if (!doc["state"].is<int>() && !doc["state"].is<long>()) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_STATE\",\"error\":\"Field 'state' must be an integer (0 or 1)\"}");
        return;
    }

    // Check for fractional floating points (e.g. 1.5)
    double stateDouble = doc["state"].as<double>();
    long rawState = doc["state"].as<long>();
    if (stateDouble != (double)rawState) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_STATE\",\"error\":\"Floating point state not accepted; numeric integer 0 or 1 required\"}");
        return;
    }

    // Explicit range check: exactly 0 or 1 (rejects 256, -1, 2, -255 before casting!)
    if (rawState != 0 && rawState != 1) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_STATE\",\"error\":\"State must be strictly numeric integer 0 (OFF) or 1 (ON)\"}");
        return;
    }

    uint8_t validatedState = (uint8_t)rawState;

    // ── Section 4: Canonical Authentication Schema ───────────────────────────
    // Reads auth_pin from JSON body (preferred canonical field)
    String pin = "";
    if (doc["auth_pin"].is<const char*>()) {
        pin = doc["auth_pin"].as<String>();
    } else if (doc["pin"].is<const char*>()) {
        pin = doc["pin"].as<String>();
    } else if (server.hasHeader("X-Operator-PIN")) {
        pin = server.header("X-Operator-PIN");
    }

    String outStatus = "";
    String outError = "";
    String outPlcEndCode = "";
    int outReadBack = -1;
    bool outReadbackVerified = false;

    bool success = plcManager.executeWrite(
        deviceId,
        pointId,
        validatedState,
        pin,
        outStatus,
        outError,
        outPlcEndCode,
        outReadBack,
        outReadbackVerified
    );

    // Track authentication attempts for rate-limiting
    if (outStatus == "UNAUTHORIZED") {
        failedPinAttempts++;
        if (failedPinAttempts >= 5) {
            lockoutUntilMs = millis() + 30000; // 30-second lockout
            failedPinAttempts = 0;
            Serial.println(F("[SECURITY] Operator PIN rate-limit exceeded: 30-second lockout activated"));
        }
    } else if (success) {
        failedPinAttempts = 0;
        lockoutUntilMs = 0;
    }

    // ── Section 6: Standardized Canonical Response Schema ─────────────────────
    JsonDocument resp;
    resp["success"] = success;
    resp["device_id"] = deviceId;
    resp["point"] = pointId;
    resp["requested_state"] = (int)validatedState;
    if (outPlcEndCode.length() > 0) {
        resp["plc_end_code"] = outPlcEndCode;
    } else {
        resp["plc_end_code"] = nullptr;
    }

    if (outReadBack >= 0) {
        resp["readback_value"] = outReadBack;
    } else {
        resp["readback_value"] = nullptr;
    }

    resp["readback_verified"] = outReadbackVerified;
    resp["status"] = outStatus;

    if (outError.length() > 0) {
        resp["error"] = outError;
    } else {
        resp["error"] = nullptr;
    }

    String respJson;
    serializeJson(resp, respJson);

    int httpCode = 200;
    if (outStatus == "UNAUTHORIZED") {
        httpCode = 401;
    } else if (outStatus == "PHYSICAL_WRITES_DISABLED") {
        httpCode = 403; // Forbidden by gateway safety policy
    } else if (outStatus == "FORBIDDEN_POINT" || outStatus == "DEVICE_NOT_FOUND") {
        httpCode = 403;
    } else if (outStatus == "INVALID_STATE" || outStatus == "MISSING_STATE") {
        httpCode = 400;
    } else if (!success) {
        httpCode = 502; // Bad Gateway / PLC write or readback mismatch
    }

    server.send(httpCode, "application/json", respJson);
}
