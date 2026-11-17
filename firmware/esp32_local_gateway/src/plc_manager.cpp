#include "plc_manager.h"
#include <WiFi.h>

PlcManager plcManager;

static void plcWorkerTaskEntry(void* param) {
    PlcManager* mgr = static_cast<PlcManager*>(param);
    mgr->workerLoop();
}

PlcManager::PlcManager() {
    qDev.deviceId = Q_PLC_ID;
    qDev.name = Q_PLC_NAME;
    qDev.model = Q_PLC_MODEL;
    qDev.ip = Q_PLC_IP;
    qDev.port = Q_PLC_PORT;
    qDev.enabled = Q_PLC_ENABLED;
    qDev.configurationVerified = true; // Q-Series parameters established in GX Works2

    rDev.deviceId = R_PLC_ID;
    rDev.name = R_PLC_NAME;
    rDev.model = R_PLC_MODEL;
    rDev.ip = R_PLC_IP;
    rDev.port = R_PLC_PORT;
    rDev.enabled = R_PLC_ENABLED;
    rDev.configurationVerified = false; // Provisional — not physically verified in lab
}

void PlcManager::begin() {
    telemetryMutex = xSemaphoreCreateMutex();
    commMutex = xSemaphoreCreateMutex();

    Serial.println(F("[PLC_MGR] Initializing Dual-PLC Communication Engine (V2.2.1)..."));
    Serial.printf("[PLC_MGR] Station 1: %s (%s:%d) [Writes: %s]\n",
        qDev.model.c_str(), qDev.ip.c_str(), qDev.port,
        Q_PLC_WRITES_ENABLED ? "ENABLED" : "DISABLED (Fail-Closed)");
    Serial.printf("[PLC_MGR] Station 2: %s (%s:%d) [Writes: %s]\n",
        rDev.model.c_str(), rDev.ip.c_str(), rDev.port,
        R_PLC_WRITES_ENABLED ? "ENABLED" : "DISABLED (Fail-Closed)");

    // Spawn dedicated FreeRTOS worker task pinned to Core 0 with 8KB stack
    // Leaves Core 1 100% dedicated to servicing HTTP web requests with sub-2ms latency
    BaseType_t res = xTaskCreatePinnedToCore(
        plcWorkerTaskEntry,
        "plc_poll_worker",
        8192,
        this,
        1,
        &workerTaskHandle,
        0 // Pin to Core 0
    );

    if (res == pdPASS) {
        Serial.println(F("[PLC_MGR] FreeRTOS background worker task started successfully on Core 0"));
    } else {
        Serial.println(F("[PLC_MGR] ERROR: Failed to create FreeRTOS worker task!"));
    }
}

void PlcManager::workerLoop() {
    while (true) {
        if (WiFi.status() == WL_CONNECTED) {
            // Attempt to acquire communication mutex (bounded 100ms wait)
            if (commMutex && xSemaphoreTake(commMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // 1. Poll Station 1: Mitsubishi Q-Series
                if (qDev.enabled) {
                    pollDevice(qDev, qClient, false);
                }

                vTaskDelay(pdMS_TO_TICKS(5)); // Yield between stations to prevent bus contention

                // 2. Poll Station 2: Mitsubishi R-Series (Independent loop)
                if (rDev.enabled) {
                    pollDevice(rDev, rClient, true);
                }

                xSemaphoreGive(commMutex);
            }
        } else {
            // Wi-Fi offline: update telemetry states safely under mutex
            if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                qDev.connected = false;
                rDev.connected = false;
                qDev.hasValidSample = false;
                rDev.hasValidSample = false;
                qDev.dataQuality = "COMM_FAILURE";
                rDev.dataQuality = "COMM_FAILURE";
                xSemaphoreGive(telemetryMutex);
            }
        }

        // Bounded poll interval yield (feeds watchdog and yields Core 0 CPU to idle task)
        vTaskDelay(pdMS_TO_TICKS(PLC_POLL_INTERVAL_MS));
    }
}

void PlcManager::loop() {
    // Non-blocking Core 1 loop: only updates staleness detection under mutex
    uint32_t now = millis();
    if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (qDev.hasValidSample && (now - qDev.diag.lastSuccessMonoMs > STALE_THRESHOLD_MS)) {
            qDev.dataQuality = "STALE";
        }
        if (rDev.hasValidSample && (now - rDev.diag.lastSuccessMonoMs > STALE_THRESHOLD_MS)) {
            rDev.dataQuality = "STALE";
        }
        xSemaphoreGive(telemetryMutex);
    }
}

DeviceTelemetry PlcManager::getQTelemetry() {
    DeviceTelemetry copy;
    if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = qDev;
        xSemaphoreGive(telemetryMutex);
    } else {
        copy = qDev;
    }
    return copy;
}

DeviceTelemetry PlcManager::getRTelemetry() {
    DeviceTelemetry copy;
    if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = rDev;
        xSemaphoreGive(telemetryMutex);
    } else {
        copy = rDev;
    }
    return copy;
}

bool PlcManager::isQConnected() {
    bool c = false;
    if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        c = qDev.connected;
        xSemaphoreGive(telemetryMutex);
    } else {
        c = qDev.connected;
    }
    return c;
}

bool PlcManager::isRConnected() {
    bool c = false;
    if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        c = rDev.connected;
        xSemaphoreGive(telemetryMutex);
    } else {
        c = rDev.connected;
    }
    return c;
}

uint32_t PlcManager::getWorkerMinFreeStack() const {
    if (!workerTaskHandle) return 0;
    return uxTaskGetStackHighWaterMark(workerTaskHandle) * sizeof(StackType_t);
}

bool PlcManager::readMcFrame(WiFiClient& client, uint8_t* buf, size_t maxLen, size_t& totalRead, uint32_t timeoutMs) {
    totalRead = 0;
    uint32_t startMs = millis();

    // Phase 1: Read minimum 11-byte header
    while (client.connected() && totalRead < 11 && (millis() - startMs < timeoutMs)) {
        if (client.available()) {
            buf[totalRead++] = client.read();
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    if (totalRead < 11) {
        return false; // Header timeout or client dropped
    }

    // Phase 2: Compute expected total frame size from bytes 7-8
    int expectedTotal = McProtocol::getExpectedResponseLength(buf, totalRead);
    if (expectedTotal < 11 || (size_t)expectedTotal > maxLen) {
        return false; // Malformed header or oversized payload
    }

    // Phase 3: Read remaining bytes (if any)
    while (client.connected() && totalRead < (size_t)expectedTotal && (millis() - startMs < timeoutMs)) {
        if (client.available()) {
            buf[totalRead++] = client.read();
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    return (totalRead == (size_t)expectedTotal);
}

void PlcManager::pollDevice(DeviceTelemetry& dev, WiFiClient& client, bool isIQR) {
    uint32_t tStart = millis();

    if (!client.connected()) {
        client.stop();
        client.setTimeout(1); // 1-second connect timeout
        if (!client.connect(dev.ip.c_str(), dev.port)) {
            if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                dev.connected = false;
                dev.hasValidSample = false;
                dev.dataQuality = "COMM_FAILURE";
                dev.diag.timeouts++;
                dev.diag.lastError = "TCP connect timeout";
                xSemaphoreGive(telemetryMutex);
            }
            return;
        }
    }

    uint8_t reqBuf[32];
    uint8_t respBuf[64];

    // ── Phase A: Read Inputs X0..X7 ──────────────────────────────────────────
    size_t reqLen = McProtocol::buildReadBitFrame(
        MC_DEV_X,
        0x00,
        8,
        isIQR,
        reqBuf,
        sizeof(reqBuf)
    );
    if (reqLen == 0) return;

    dev.diag.packetsSent++;
    size_t written = client.write(reqBuf, reqLen);
    client.flush();
    if (written != reqLen) {
        client.stop();
        if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dev.connected = false;
            dev.hasValidSample = false;
            dev.dataQuality = "COMM_FAILURE";
            dev.diag.timeouts++;
            dev.diag.lastError = "Incomplete TCP write for inputs";
            xSemaphoreGive(telemetryMutex);
        }
        return;
    }

    size_t bytesRead = 0;
    if (!readMcFrame(client, respBuf, sizeof(respBuf), bytesRead, 300)) {
        client.stop();
        if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dev.connected = false;
            dev.hasValidSample = false;
            dev.dataQuality = "COMM_FAILURE";
            dev.diag.timeouts++;
            dev.diag.lastError = "Input read timeout";
            xSemaphoreGive(telemetryMutex);
        }
        return;
    }

    uint8_t rawInputs[8] = {0};
    int err = McProtocol::parseReadBitResponse(respBuf, bytesRead, 8, rawInputs);
    if (err != 0) {
        if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dev.diag.protoErrors++;
            dev.diag.lastError = "Input MC error 0x" + String(err, HEX);
            dev.dataQuality = "COMM_FAILURE";
            xSemaphoreGive(telemetryMutex);
        }
        return;
    }

    dev.diag.packetsReceived++;

    vTaskDelay(pdMS_TO_TICKS(2)); // Cooperative yield between phases

    // ── Phase B: Read Outputs Y20..Y27 ────────────────────────────────────────
    reqLen = McProtocol::buildReadBitFrame(
        MC_DEV_Y,
        Q_PLC_OUTPUT_BASE, // 0x20 = Y20
        8,
        isIQR,
        reqBuf,
        sizeof(reqBuf)
    );
    if (reqLen == 0) return;

    dev.diag.packetsSent++;
    written = client.write(reqBuf, reqLen);
    client.flush();
    if (written != reqLen) {
        client.stop();
        if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dev.connected = false;
            dev.hasValidSample = false;
            dev.dataQuality = "COMM_FAILURE";
            dev.diag.timeouts++;
            dev.diag.lastError = "Incomplete TCP write for outputs";
            xSemaphoreGive(telemetryMutex);
        }
        return;
    }

    bytesRead = 0;
    if (!readMcFrame(client, respBuf, sizeof(respBuf), bytesRead, 300)) {
        client.stop();
        if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dev.connected = false;
            dev.hasValidSample = false;
            dev.dataQuality = "COMM_FAILURE";
            dev.diag.timeouts++;
            dev.diag.lastError = "Output read timeout";
            xSemaphoreGive(telemetryMutex);
        }
        return;
    }

    uint8_t rawOutputs[8] = {0};
    err = McProtocol::parseReadBitResponse(respBuf, bytesRead, 8, rawOutputs);
    if (err != 0) {
        if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            dev.diag.protoErrors++;
            dev.diag.lastError = "Output MC error 0x" + String(err, HEX);
            dev.dataQuality = "COMM_FAILURE";
            xSemaphoreGive(telemetryMutex);
        }
        return;
    }

    dev.diag.packetsReceived++;

    // Both phases succeeded: atomically establish GOOD telemetry state under mutex
    if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < 8; i++) {
            dev.inputs[i] = rawInputs[i];
            dev.outputs[i] = rawOutputs[i];
        }
        dev.connected = true;
        dev.hasValidSample = true;
        dev.dataQuality = "GOOD";
        dev.diag.lastSuccessMonoMs = millis();
        dev.diag.lastLatencyMs = (float)(millis() - tStart);
        dev.diag.lastError = "";
        xSemaphoreGive(telemetryMutex);
    }
}

bool PlcManager::executeWrite(
    const String& deviceId,
    const String& pointId,
    uint8_t requestedState,
    const String& authPin,
    String& outStatus,
    String& outError,
    String& outPlcEndCode,
    int& outReadBack,
    bool& outReadbackVerified
) {
    String cleanPoint = pointId;
    cleanPoint.toUpperCase();
    cleanPoint.trim();

    outPlcEndCode = "";
    outReadBack = -1;
    outReadbackVerified = false;

    // Fail-Closed Gate 1: Check physical write permission
    if (deviceId == Q_PLC_ID) {
        if (!Q_PLC_WRITES_ENABLED) {
            outStatus = "PHYSICAL_WRITES_DISABLED";
            outError = "Q-Series physical writes are disabled by default in gateway configuration (Fail-Closed safety gate).";
            return false;
        }
    } else if (deviceId == R_PLC_ID) {
        if (!R_PLC_WRITES_ENABLED) {
            outStatus = "PHYSICAL_WRITES_DISABLED";
            outError = "R-Series physical writes are disabled by default in gateway configuration (Fail-Closed safety gate).";
            return false;
        }
    } else {
        outStatus = "DEVICE_NOT_FOUND";
        outError = "Unknown device ID: " + deviceId;
        return false;
    }

    // Fail-Closed Gate 2: Banned Point Y0
    if (cleanPoint == "Y0" || cleanPoint == "Y00" || cleanPoint == "Y000") {
        outStatus = "FORBIDDEN_POINT";
        outError = "Point Y0 is strictly forbidden by project safety rules.";
        Serial.println(F("[SAFETY] Blocked unauthorized attempt to write to Y0!"));
        return false;
    }

    // Fail-Closed Gate 3: PIN Authorization Check
    // If SECRET_OPERATOR_PIN is empty, not set, or placeholder, fail closed!
    if (strlen(SECRET_OPERATOR_PIN) == 0 ||
        strcmp(SECRET_OPERATOR_PIN, "SET_PRIVATE_OPERATOR_PIN") == 0 ||
        strcmp(SECRET_OPERATOR_PIN, "CHANGE_ME") == 0) {
        outStatus = "UNAUTHORIZED";
        outError = "Operator authorization PIN is not configured on gateway (fails closed).";
        return false;
    }

    if (authPin.length() == 0 || authPin != SECRET_OPERATOR_PIN) {
        outStatus = "UNAUTHORIZED";
        outError = "Invalid or missing operator authorization PIN.";
        return false;
    }

    // Fail-Closed Gate 4: State Validation
    if (requestedState > 1) {
        outStatus = "INVALID_STATE";
        outError = "State must be strictly 0 (OFF) or 1 (ON).";
        return false;
    }

    // Fail-Closed Gate 5: Allowlist Verification
    DeviceTelemetry* targetDev = nullptr;
    WiFiClient* targetClient = nullptr;
    bool isIQR = false;
    uint32_t pointAddr = 0;

    if (deviceId == Q_PLC_ID) {
        targetDev = &qDev;
        targetClient = &qClient;
        isIQR = false;
        // Whitelist for Q-Series: 8 independent outputs Y20 - Y27 (Hex 0x20 - 0x27)
        if (!McProtocol::parseQOutputPoint(cleanPoint.c_str(), pointAddr)) {
            outStatus = "FORBIDDEN_POINT";
            outError = "Point '" + cleanPoint + "' is not permitted. Authorized: Y20 through Y27.";
            return false;
        }
    } else {
        targetDev = &rDev;
        targetClient = &rClient;
        isIQR = true;
        if (!McProtocol::parseQOutputPoint(cleanPoint.c_str(), pointAddr)) {
            outStatus = "FORBIDDEN_POINT";
            outError = "Point '" + cleanPoint + "' is not permitted.";
            return false;
        }
    }

    // Fail-Closed Gate 6: Target PLC must be enabled
    if (!targetDev->enabled) {
        outStatus = "COMM_FAILURE";
        outError = "Target PLC device is disabled.";
        return false;
    }

    // Phase 6: Mutual Exclusion on Socket via FreeRTOS commMutex (bounded wait 3500ms)
    if (commMutex && xSemaphoreTake(commMutex, pdMS_TO_TICKS(3500)) != pdTRUE) {
        outStatus = "COMM_BUSY";
        outError = "PLC communication channel busy or timeout.";
        return false;
    }

    bool writeSuccess = executeSocketWriteAndReadback(
        *targetDev,
        *targetClient,
        MC_DEV_Y,
        pointAddr,
        requestedState,
        isIQR,
        outPlcEndCode,
        outReadBack,
        outReadbackVerified,
        outError
    );

    if (writeSuccess) {
        outStatus = "OUTPUT_CONFIRMED";
        outError = "";
        // Update local telemetry cache ONLY for this point (isolate neighboring points)
        int outIdx = McProtocol::getQOutputIndex(pointAddr);
        if (outIdx >= 0 && outIdx < 8) {
            if (telemetryMutex && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                targetDev->outputs[outIdx] = requestedState;
                xSemaphoreGive(telemetryMutex);
            }
        }
        Serial.printf("[CONTROL] Actuation of %s (0x%02X) on %s verified -> Readback: %d\n",
            cleanPoint.c_str(), (unsigned int)pointAddr, deviceId.c_str(), outReadBack);
        xSemaphoreGive(commMutex);
        return true;
    } else {
        if (outStatus.length() == 0) {
            outStatus = "WRITE_FAILED";
        }
        xSemaphoreGive(commMutex);
        return false;
    }
}

bool PlcManager::executeSocketWriteAndReadback(
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
) {
    if (!client.connected()) {
        client.stop();
        client.setTimeout(2);
        if (!client.connect(dev.ip.c_str(), dev.port)) {
            outError = "Cannot connect to PLC at " + dev.ip + ":" + String(dev.port);
            return false;
        }
    }

    // Step 1: Send MC Protocol Batch Write frame (0x1401)
    uint8_t writeFrame[32];
    size_t writeLen = McProtocol::buildWriteBitFrame(
        devCode,
        headAddr,
        state,
        isIQR,
        writeFrame,
        sizeof(writeFrame)
    );
    if (writeLen == 0) {
        outError = "Failed to construct write frame (rejected by safety policy)";
        return false;
    }

    dev.diag.packetsSent++;
    size_t written = client.write(writeFrame, writeLen);
    client.flush();
    if (written != writeLen) {
        client.stop();
        outError = "Incomplete TCP transmission during write";
        return false;
    }

    // Read write response (11 bytes: 0xD0 0x00 ... endcode)
    uint8_t respBuf[32];
    size_t bytesRead = 0;
    if (!readMcFrame(client, respBuf, sizeof(respBuf), bytesRead, 500)) {
        client.stop();
        outError = "Write response timeout from PLC";
        return false;
    }

    dev.diag.packetsReceived++;
    int writeEndCode = McProtocol::parseWriteResponse(respBuf, bytesRead);
    char endCodeHex[16];
    snprintf(endCodeHex, sizeof(endCodeHex), "0x%04X", (unsigned int)writeEndCode);
    outPlcEndCode = String(endCodeHex);

    if (writeEndCode != 0) {
        outError = "PLC rejected write with MC error " + outPlcEndCode;
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(5)); // Cooperative yield before read-back

    // Step 2: Immediate Read-Back Verification (0x0401)
    uint8_t readFrame[32];
    size_t readLen = McProtocol::buildReadBitFrame(
        devCode,
        headAddr,
        1,
        isIQR,
        readFrame,
        sizeof(readFrame)
    );
    if (readLen == 0) {
        outError = "Failed to construct read-back frame";
        return false;
    }

    dev.diag.packetsSent++;
    written = client.write(readFrame, readLen);
    client.flush();
    if (written != readLen) {
        client.stop();
        outError = "Incomplete TCP transmission during read-back";
        return false;
    }

    bytesRead = 0;
    if (!readMcFrame(client, respBuf, sizeof(respBuf), bytesRead, 500)) {
        client.stop();
        outError = "Read-back response timeout from PLC";
        return false;
    }

    dev.diag.packetsReceived++;
    uint8_t readBits[1] = {0};
    int readEndCode = McProtocol::parseReadBitResponse(respBuf, bytesRead, 1, readBits);
    if (readEndCode != 0) {
        outError = "Read-back rejected by PLC with MC error 0x" + String(readEndCode, HEX);
        return false;
    }

    outReadBack = (int)readBits[0];
    outReadbackVerified = (outReadBack == state);

    if (!outReadbackVerified) {
        outError = "Read-back mismatch: commanded " + String(state) + ", read " + String(outReadBack);
        return false;
    }

    return true;
}
