#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
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
    String dataQuality = "UNKNOWN"; // "GOOD", "COMM_FAILURE", "STALE", "UNKNOWN"
    bool configurationVerified = false;
    
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

    // Thread-safe copy getters for API (non-blocking)
    DeviceTelemetry getQTelemetry();
    DeviceTelemetry getRTelemetry();
    bool isQConnected();
    bool isRConnected();
    uint32_t getWorkerMinFreeStack() const;

    // Controlled Output Execution with Multi-Gate Safety & Read-Back Verification
    bool executeWrite(
        const String& deviceId,
        const String& pointId,
        uint8_t requestedState,
        const String& authPin,
        String& outStatus,
        String& outError,
        String& outPlcEndCode,
        int& outReadBack,
        bool& outReadbackVerified
    );

    // FreeRTOS background worker loop (runs on Core 0)
    void workerLoop();

private:
    DeviceTelemetry qDev;
    DeviceTelemetry rDev;

    WiFiClient qClient;
    WiFiClient rClient;

    SemaphoreHandle_t telemetryMutex = nullptr;
    SemaphoreHandle_t commMutex = nullptr;
    TaskHandle_t workerTaskHandle = nullptr;

    void pollDevice(DeviceTelemetry& dev, WiFiClient& client, bool isIQR);
    
    // Bounded MC Protocol frame reader
    bool readMcFrame(WiFiClient& client, uint8_t* buf, size_t maxLen, size_t& totalRead, uint32_t timeoutMs);

    bool executeSocketWriteAndReadback(
        DeviceTelemetry& dev,
        WiFiClient& client,
        uint8_t devCode,
        uint32_t headAddr,
        uint8_t state,
        bool isIQR,
        String& outPlcEndCode,
        int& outReadBack,
        bool& outReadbackVerified,
        String& outError
    );
};

extern PlcManager plcManager;
