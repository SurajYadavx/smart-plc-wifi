#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include "config.h"
#include "mc_protocol.h"

struct DeviceDiagnostics {
    uint32_t packetsSent = 0;
    uint32_t packetsReceived = 0;
    uint32_t timeouts = 0;
    uint32_t protoErrors = 0;
    float lastLatencyMs = 0.0f;
    String lastError = "";
    uint32_t lastSuccessMonoMs = 0;
};

struct DeviceTelemetry {
    String deviceId;
    String name;
    String model;
    String ip;
    uint16_t port;
    bool enabled = true;
    bool connected = false;
    bool hasValidSample = false;
    String dataQuality = "UNCERTAIN"; // "GOOD", "COMM_FAILURE", "STALE", "UNCERTAIN"
    
    // Digital inputs & outputs (0 or 1)
    uint8_t inputs[8] = {0};
    uint8_t outputs[8] = {0};
    
    DeviceDiagnostics diag;
};

class PlcManager {
public:
    PlcManager();
    void begin();
    void loop();

    // Getters for API
    const DeviceTelemetry& getQTelemetry() const { return qDev; }
    const DeviceTelemetry& getRTelemetry() const { return rDev; }
    bool isQConnected() const { return qDev.connected; }
    bool isRConnected() const { return rDev.connected; }

    // Controlled Output Execution with Allowlist & Read-Back Verification
    bool executeWrite(
        const String& deviceId,
        const String& pointId,
        uint8_t requestedState,
        const String& authPin,
        String& outStatus,
        String& outError,
        int& outReadBack
    );

private:
    DeviceTelemetry qDev;
    DeviceTelemetry rDev;

    WiFiClient qClient;
    WiFiClient rClient;

    uint32_t lastPollCycleMs = 0;
    bool pollBusy = false;

    void pollDevice(DeviceTelemetry& dev, WiFiClient& client, bool isIQR);
    bool executeSocketWriteAndReadback(
        DeviceTelemetry& dev,
        uint8_t devCode,
        uint32_t headAddr,
        uint8_t state,
        bool isIQR,
        int& outReadBack,
        String& outError
    );
};

extern PlcManager plcManager;
