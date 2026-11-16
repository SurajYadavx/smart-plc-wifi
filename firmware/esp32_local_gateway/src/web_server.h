#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "plc_manager.h"

class GatewayWebServer {
public:
    GatewayWebServer();
    void begin();
    void loop();

private:
    WebServer server;
    bool fsMounted = false;

    void registerRoutes();
    void handleCorsPreflight();
    void sendCorsHeaders();

    // Static Asset Handlers
    bool handleFileRead(String path);
    void serveFallbackIndex();

    // REST API Route Handlers
    void handleApiSystem();
    void handleApiDevices();
    void handleApiDeviceDetail(const String& deviceId);
    void handleApiTelemetry(const String& deviceId);
    void handleApiDiagnostics(const String& deviceId);
    void handleApiOutputCommand(const String& deviceId, const String& pointId);

    String getContentType(const String& filename);
};

extern GatewayWebServer gatewayWebServer;
