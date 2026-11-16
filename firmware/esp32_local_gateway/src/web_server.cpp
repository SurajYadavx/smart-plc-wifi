#include "web_server.h"
#include <WiFi.h>

GatewayWebServer gatewayWebServer;

GatewayWebServer::GatewayWebServer() : server(80) {}

void GatewayWebServer::begin() {
    Serial.println(F("[HTTP] Initializing LittleFS filesystem..."));
    if (LittleFS.begin(true)) {
        fsMounted = true;
        Serial.printf("[HTTP] LittleFS mounted successfully. Total: %u KB, Used: %u KB\n",
            (unsigned int)(LittleFS.totalBytes() / 1024),
            (unsigned int)(LittleFS.usedBytes() / 1024)
        );
    } else {
        fsMounted = false;
        Serial.println(F("[HTTP] WARNING: LittleFS mount failed — will serve fallback embedded dashboard"));
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
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
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
    
    // GET /api/v1/system
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

    // POST /api/v1/devices/melsec_q03/outputs/Y20/command
    server.on("/api/v1/devices/" Q_PLC_ID "/outputs/Y20/command", HTTP_POST, [this]() {
        handleApiOutputCommand(Q_PLC_ID, "Y20");
    });
    server.on("/api/v1/devices/" Q_PLC_ID "/outputs/Y0/command", HTTP_POST, [this]() {
        handleApiOutputCommand(Q_PLC_ID, "Y0");
    });

    // R-Series output command endpoint
    server.on("/api/v1/devices/" R_PLC_ID "/outputs/Y20/command", HTTP_POST, [this]() {
        handleApiOutputCommand(R_PLC_ID, "Y20");
    });
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
        "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32 Dual-PLC Gateway v2.0</title>"
        "<style>body{background:#0d1117;color:#e6edf3;font-family:system-ui,sans-serif;padding:2rem;line-height:1.5}"
        "h1{color:#58a6ff;font-size:1.4rem;border-bottom:1px solid #30363d;padding-bottom:.5rem}"
        ".card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:1.2rem;margin-top:1rem}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:.75rem;font-weight:700}"
        ".badge-ok{background:rgba(63,185,80,.2);color:#3fb950}"
        ".badge-warn{background:rgba(210,153,34,.2);color:#d29922}"
        "a{color:#58a6ff;text-decoration:none}a:hover{text-decoration:underline}"
        "pre{background:#090d13;padding:.8rem;border-radius:6px;overflow-x:auto;color:#79c0ff;font-size:.85rem}"
        "</style></head><body>"
        "<h1>Q/R Series Dual-PLC Local Gateway</h1>"
        "<div class=\"card\">"
        "<h3>Gateway Status: <span class=\"badge badge-ok\">RUNNING</span></h3>"
        "<p>The ESP32 local gateway is active on the RUBY network. Static frontend assets can be loaded from LittleFS or hosted externally.</p>"
        "<ul>"
        "<li><strong>System API:</strong> <a href=\"/api/v1/system\">/api/v1/system</a></li>"
        "<li><strong>Devices List:</strong> <a href=\"/api/v1/devices\">/api/v1/devices</a></li>"
        "<li><strong>Station 1 (Q03) Telemetry:</strong> <a href=\"/api/v1/devices/melsec_q03/telemetry\">/api/v1/devices/melsec_q03/telemetry</a></li>"
        "<li><strong>Station 2 (R04) Telemetry:</strong> <a href=\"/api/v1/devices/melsec_r04/telemetry\">/api/v1/devices/melsec_r04/telemetry</a></li>"
        "<li><strong>Diagnostics:</strong> <a href=\"/api/v1/devices/melsec_q03/diagnostics\">/api/v1/devices/melsec_q03/diagnostics</a></li>"
        "</ul></div></body></html>"
    );
    server.send(200, "text/html", html);
}

// ── REST API Route Handlers ───────────────────────────────────────────────────

void GatewayWebServer::handleApiSystem() {
    sendCorsHeaders();

    JsonDocument doc;
    doc["gateway_id"] = GATEWAY_ID;
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["hardware"] = "ESP32 DevKit";

    JsonObject net = doc["network"].to<JsonObject>();
    net["ssid"] = SECRET_WIFI_SSID;
    net["ip"] = WiFi.localIP().toString();
    net["mac"] = WiFi.macAddress();
    net["gateway"] = WiFi.gatewayIP().toString();
    net["subnet"] = WiFi.subnetMask().toString();
    net["rssi_dbm"] = WiFi.RSSI();

    doc["uptime_sec"] = millis() / 1000;
    doc["free_heap_bytes"] = ESP.getFreeHeap();
    doc["configured_devices_count"] = 2;
    doc["active_devices_count"] = (plcManager.isQConnected() ? 1 : 0) + (plcManager.isRConnected() ? 1 : 0);
    doc["simulation_mode"] = false;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiDevices() {
    sendCorsHeaders();

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    const auto& q = plcManager.getQTelemetry();
    JsonObject qObj = arr.add<JsonObject>();
    qObj["id"] = q.deviceId;
    qObj["name"] = q.name;
    qObj["model"] = q.model;
    qObj["enabled"] = q.enabled;
    qObj["host"] = q.ip;
    qObj["port"] = q.port;
    qObj["protocol"] = "MC_3E_BINARY";
    qObj["status"] = q.connected ? "ONLINE" : "OFFLINE";
    qObj["data_quality"] = q.dataQuality;
    qObj["points_count"] = 16;
    JsonArray qPerm = qObj["permitted_outputs"].to<JsonArray>();
    qPerm.add("Y20");

    const auto& r = plcManager.getRTelemetry();
    JsonObject rObj = arr.add<JsonObject>();
    rObj["id"] = r.deviceId;
    rObj["name"] = r.name;
    rObj["model"] = r.model;
    rObj["enabled"] = r.enabled;
    rObj["host"] = r.ip;
    rObj["port"] = r.port;
    rObj["protocol"] = "SLMP_3E_BINARY";
    rObj["status"] = r.connected ? "ONLINE" : "UNVERIFIED";
    rObj["data_quality"] = r.dataQuality;
    rObj["points_count"] = 16;
    rObj["permitted_outputs"].to<JsonArray>(); // Empty array

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiDeviceDetail(const String& deviceId) {
    sendCorsHeaders();

    const DeviceTelemetry* dev = (deviceId == Q_PLC_ID) ? &plcManager.getQTelemetry() :
                                 (deviceId == R_PLC_ID) ? &plcManager.getRTelemetry() : nullptr;

    if (!dev) {
        server.send(404, "application/json", "{\"error\":true,\"message\":\"Device not found\"}");
        return;
    }

    JsonDocument doc;
    doc["id"] = dev->deviceId;
    doc["name"] = dev->name;
    doc["model"] = dev->model;
    doc["host"] = dev->ip;
    doc["port"] = dev->port;
    doc["protocol"] = (deviceId == Q_PLC_ID) ? "MC_3E_BINARY" : "SLMP_3E_BINARY";
    doc["status"] = dev->connected ? "ONLINE" : "OFFLINE";
    doc["data_quality"] = dev->dataQuality;

    JsonArray permitted = doc["permitted_outputs"].to<JsonArray>();
    if (deviceId == Q_PLC_ID) {
        permitted.add("Y20");
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiTelemetry(const String& deviceId) {
    sendCorsHeaders();

    const DeviceTelemetry* dev = (deviceId == Q_PLC_ID) ? &plcManager.getQTelemetry() :
                                 (deviceId == R_PLC_ID) ? &plcManager.getRTelemetry() : nullptr;

    if (!dev) {
        server.send(404, "application/json", "{\"error\":true,\"message\":\"Device not found\"}");
        return;
    }

    JsonDocument doc;
    doc["device_id"] = dev->deviceId;
    doc["connected"] = dev->connected;
    doc["quality"] = dev->dataQuality;
    doc["has_valid_sample"] = dev->hasValidSample;

    if (dev->hasValidSample) {
        doc["age_ms"] = millis() - dev->diag.lastSuccessMonoMs;
    } else {
        doc["age_ms"] = nullptr;
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

        if (dev->connected && dev->hasValidSample) {
            xPt["raw_value"] = (int)dev->inputs[i];
            xPt["quality"] = dev->dataQuality;
            xPt["error"] = nullptr;
        } else {
            xPt["raw_value"] = nullptr;
            xPt["quality"] = "COMM_FAILURE";
            xPt["error"] = dev->diag.lastError.length() > 0 ? dev->diag.lastError : "PLC Communication Failure";
        }
    }

    for (int i = 0; i < 8; i++) {
        String yKey = "Y" + String(0x20 + i, HEX);
        yKey.toUpperCase();
        JsonObject yPt = readings[yKey].to<JsonObject>();
        yPt["point_id"] = yKey;
        yPt["address"] = yKey;
        yPt["data_type"] = "BOOLEAN";

        if (dev->connected && dev->hasValidSample) {
            yPt["raw_value"] = (int)dev->outputs[i];
            yPt["quality"] = dev->dataQuality;
            yPt["error"] = nullptr;
        } else {
            yPt["raw_value"] = nullptr;
            yPt["quality"] = "COMM_FAILURE";
            yPt["error"] = dev->diag.lastError.length() > 0 ? dev->diag.lastError : "PLC Communication Failure";
        }
    }

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiDiagnostics(const String& deviceId) {
    sendCorsHeaders();

    const DeviceTelemetry* dev = (deviceId == Q_PLC_ID) ? &plcManager.getQTelemetry() :
                                 (deviceId == R_PLC_ID) ? &plcManager.getRTelemetry() : nullptr;

    if (!dev) {
        server.send(404, "application/json", "{\"error\":true,\"message\":\"Device not found\"}");
        return;
    }

    JsonDocument doc;
    doc["device_id"] = dev->deviceId;
    doc["connected"] = dev->connected;
    doc["packets_sent"] = dev->diag.packetsSent;
    doc["packets_received"] = dev->diag.packetsReceived;
    doc["timeouts"] = dev->diag.timeouts;
    doc["proto_errors"] = dev->diag.protoErrors;
    doc["last_latency_ms"] = dev->diag.lastLatencyMs;
    doc["last_error"] = dev->diag.lastError.length() > 0 ? dev->diag.lastError : nullptr;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void GatewayWebServer::handleApiOutputCommand(const String& deviceId, const String& pointId) {
    sendCorsHeaders();

    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"INVALID_JSON\",\"error\":\"Malformed JSON payload\"}");
        return;
    }

    if (!doc.containsKey("state") || doc["state"].isNull()) {
        server.send(400, "application/json", "{\"success\":false,\"status\":\"MISSING_STATE\",\"error\":\"Field 'state' is required (0 or 1)\"}");
        return;
    }

    int state = doc["state"].as<int>();
    String pin = doc["auth_pin"].is<const char*>() ? doc["auth_pin"].as<String>() :
                 doc["auth_token"].is<const char*>() ? doc["auth_token"].as<String>() : "";

    String outStatus = "";
    String outError = "";
    int outReadBack = -1;

    bool success = plcManager.executeWrite(
        deviceId,
        pointId,
        (uint8_t)state,
        pin,
        outStatus,
        outError,
        outReadBack
    );

    JsonDocument resp;
    resp["success"] = success;
    resp["device_id"] = deviceId;
    resp["point"] = pointId;
    resp["requested_state"] = state;
    resp["read_back_state"] = (outReadBack >= 0) ? outReadBack : -1;
    resp["verified"] = success;
    resp["status"] = outStatus;
    resp["error"] = outError.length() > 0 ? outError : nullptr;

    String respJson;
    serializeJson(resp, respJson);

    int httpCode = 200;
    if (outStatus == "UNAUTHORIZED") {
        httpCode = 401;
    } else if (outStatus == "FORBIDDEN_POINT" || outStatus == "DEVICE_NOT_FOUND") {
        httpCode = 403;
    } else if (!success) {
        httpCode = 502; // Bad Gateway / Write execution failure
    }

    server.send(httpCode, "application/json", respJson);
}
