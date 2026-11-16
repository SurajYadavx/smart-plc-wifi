#include "plc_manager.h"
#include <WiFi.h>

PlcManager plcManager;

PlcManager::PlcManager() {
    qDev.deviceId = Q_PLC_ID;
    qDev.name = Q_PLC_NAME;
    qDev.model = Q_PLC_MODEL;
    qDev.ip = Q_PLC_IP;
    qDev.port = Q_PLC_PORT;
    qDev.enabled = Q_PLC_ENABLED;

    rDev.deviceId = R_PLC_ID;
    rDev.name = R_PLC_NAME;
    rDev.model = R_PLC_MODEL;
    rDev.ip = R_PLC_IP;
    rDev.port = R_PLC_PORT;
    rDev.enabled = R_PLC_ENABLED;
}

void PlcManager::begin() {
    Serial.println(F("[PLC_MGR] Initializing Dual-PLC Communication Engine..."));
    Serial.printf("[PLC_MGR] Station 1: %s (%s:%d)\n", qDev.model.c_str(), qDev.ip.c_str(), qDev.port);
    Serial.printf("[PLC_MGR] Station 2: %s (%s:%d)\n", rDev.model.c_str(), rDev.ip.c_str(), rDev.port);
}

void PlcManager::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        qDev.connected = false;
        rDev.connected = false;
        qDev.dataQuality = "COMM_FAILURE";
        rDev.dataQuality = "COMM_FAILURE";
        return;
    }

    uint32_t now = millis();

    // Check for stale telemetry on active devices
    if (qDev.hasValidSample && (now - qDev.diag.lastSuccessMonoMs > STALE_THRESHOLD_MS)) {
        qDev.dataQuality = "STALE";
    }
    if (rDev.hasValidSample && (now - rDev.diag.lastSuccessMonoMs > STALE_THRESHOLD_MS)) {
        rDev.dataQuality = "STALE";
    }

    if (now - lastPollCycleMs >= PLC_POLL_INTERVAL_MS) {
        lastPollCycleMs = now;

        if (!pollBusy) {
            pollBusy = true;
            // 1. Poll Station 1: Mitsubishi Q-Series
            if (qDev.enabled) {
                pollDevice(qDev, qClient, false);
            }
            // 2. Poll Station 2: Mitsubishi R-Series (Independent loop)
            if (rDev.enabled) {
                pollDevice(rDev, rClient, true);
            }
            pollBusy = false;
        }
    }
}

void PlcManager::pollDevice(DeviceTelemetry& dev, WiFiClient& client, bool isIQR) {
    uint32_t tStart = millis();

    if (!client.connected()) {
        client.stop();
        client.setTimeout(2); // 2 seconds connect timeout
        if (!client.connect(dev.ip.c_str(), dev.port)) {
            dev.connected = false;
            dev.dataQuality = "COMM_FAILURE";
            dev.diag.timeouts++;
            dev.diag.lastError = "TCP connect timeout";
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
    client.write(reqBuf, reqLen);
    client.flush();

    size_t bytesRead = 0;
    uint32_t waitStart = millis();
    while (client.connected() && bytesRead < 15 && (millis() - waitStart < 1500)) {
        if (client.available()) {
            respBuf[bytesRead++] = client.read();
        } else {
            delay(2);
        }
    }

    if (bytesRead < 15) {
        client.stop();
        dev.connected = false;
        dev.dataQuality = "COMM_FAILURE";
        dev.diag.timeouts++;
        dev.diag.lastError = "Input read timeout";
        return;
    }

    uint8_t rawInputs[8] = {0};
    int err = McProtocol::parseReadBitResponse(respBuf, bytesRead, 8, rawInputs);
    if (err != 0) {
        dev.diag.protoErrors++;
        dev.diag.lastError = "Input MC error 0x" + String(err, HEX);
        dev.dataQuality = "COMM_FAILURE";
        return;
    }

    dev.diag.packetsReceived++;
    for (int i = 0; i < 8; i++) {
        dev.inputs[i] = rawInputs[i];
    }

    // ── Phase B: Read Outputs Y20..Y27 ────────────────────────────────────────
    reqLen = McProtocol::buildReadBitFrame(
        MC_DEV_Y,
        Q_PLC_OUTPUT_BASE, // 0x20 = Y20
        8,
        isIQR,
        reqBuf,
        sizeof(reqBuf)
    );

    dev.diag.packetsSent++;
    client.write(reqBuf, reqLen);
    client.flush();

    bytesRead = 0;
    waitStart = millis();
    while (client.connected() && bytesRead < 15 && (millis() - waitStart < 1500)) {
        if (client.available()) {
            respBuf[bytesRead++] = client.read();
        } else {
            delay(2);
        }
    }

    if (bytesRead < 15) {
        client.stop();
        dev.connected = false;
        dev.dataQuality = "COMM_FAILURE";
        dev.diag.timeouts++;
        dev.diag.lastError = "Output read timeout";
        return;
    }

    uint8_t rawOutputs[8] = {0};
    err = McProtocol::parseReadBitResponse(respBuf, bytesRead, 8, rawOutputs);
    if (err != 0) {
        dev.diag.protoErrors++;
        dev.diag.lastError = "Output MC error 0x" + String(err, HEX);
        dev.dataQuality = "COMM_FAILURE";
        return;
    }

    dev.diag.packetsReceived++;
    for (int i = 0; i < 8; i++) {
        dev.outputs[i] = rawOutputs[i];
    }

    // Successfully sampled
    dev.connected = true;
    dev.hasValidSample = true;
    dev.dataQuality = "GOOD";
    dev.diag.lastError = "";
    dev.diag.lastSuccessMonoMs = millis();
    dev.diag.lastLatencyMs = (float)(millis() - tStart);
}

bool PlcManager::executeWrite(
    const String& deviceId,
    const String& pointId,
    uint8_t requestedState,
    const String& authPin,
    String& outStatus,
    String& outError,
    int& outReadBack
) {
    String cleanPoint = pointId;
    cleanPoint.trim();
    cleanPoint.toUpperCase();
    outReadBack = -1;

    // 1. Safety Check: Banned Point Y0
    if (cleanPoint == "Y0" || cleanPoint == "Y00" || cleanPoint == "Y000") {
        outStatus = "FORBIDDEN_POINT";
        outError = "Point Y0 is strictly forbidden by project safety rules.";
        Serial.println(F("[SAFETY] Blocked unauthorized attempt to write to Y0!"));
        return false;
    }

    // 2. PIN Authorization Check
    if (authPin != SECRET_OPERATOR_PIN) {
        outStatus = "UNAUTHORIZED";
        outError = "Invalid operator authorization PIN.";
        Serial.println(F("[SECURITY] Rejected write: Invalid authorization PIN."));
        return false;
    }

    // 3. State Validation
    if (requestedState > 1) {
        outStatus = "INVALID_STATE";
        outError = "State must be 0 (OFF) or 1 (ON).";
        return false;
    }

    // 4. Target Device & Allowlist Verification
    DeviceTelemetry* targetDev = nullptr;
    bool isIQR = false;
    uint32_t pointAddr = 0;

    if (deviceId == Q_PLC_ID) {
        targetDev = &qDev;
        isIQR = false;
        // Whitelist for Q-Series: only Y20 (0x20)
        if (cleanPoint != "Y20") {
            outStatus = "FORBIDDEN_POINT";
            outError = "Point '" + cleanPoint + "' is not permitted. Allowed: Y20 only.";
            return false;
        }
        pointAddr = Q_PERMITTED_OUTPUT_ADDR;
    } else if (deviceId == R_PLC_ID) {
        targetDev = &rDev;
        isIQR = true;
        // R-Series output writes are disabled by default until physical verification
        if (!R_PLC_WRITES_ENABLED) {
            outStatus = "FORBIDDEN_POINT";
            outError = "R-Series output writes disabled pending laboratory hardware commissioning.";
            return false;
        }
        if (cleanPoint != "Y20") {
            outStatus = "FORBIDDEN_POINT";
            outError = "R-Series point not permitted.";
            return false;
        }
        pointAddr = Q_PERMITTED_OUTPUT_ADDR;
    } else {
        outStatus = "DEVICE_NOT_FOUND";
        outError = "Unknown device ID: " + deviceId;
        return false;
    }

    // 5. Execute Write with Read-Back Verification
    pollBusy = true; // Pause polling cycle to avoid socket contention
    bool writeSuccess = executeSocketWriteAndReadback(
        *targetDev,
        MC_DEV_Y,
        pointAddr,
        requestedState,
        isIQR,
        outReadBack,
        outError
    );
    pollBusy = false;

    if (writeSuccess) {
        outStatus = "OUTPUT_CONFIRMED";
        outError = "";
        // Update local telemetry cache for this point
        if (pointAddr == 0x20) {
            targetDev->outputs[0] = requestedState;
        }
        Serial.printf("[CONTROL] Successfully actuated %s on %s -> Verified State: %d\n",
            cleanPoint.c_str(), deviceId.c_str(), outReadBack);
        return true;
    } else {
        outStatus = "WRITE_FAILED";
        Serial.printf("[CONTROL] Write failed for %s on %s: %s\n",
            cleanPoint.c_str(), deviceId.c_str(), outError.c_str());
        return false;
    }
}

bool PlcManager::executeSocketWriteAndReadback(
    DeviceTelemetry& dev,
    uint8_t devCode,
    uint32_t headAddr,
    uint8_t state,
    bool isIQR,
    int& outReadBack,
    String& outError
) {
    WiFiClient client;
    client.setTimeout(3);

    if (!client.connect(dev.ip.c_str(), dev.port)) {
        outError = "Cannot connect to PLC at " + dev.ip + ":" + String(dev.port);
        return false;
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
        outError = "Failed to construct write frame";
        client.stop();
        return false;
    }

    client.write(writeFrame, writeLen);
    client.flush();

    // Read write response (11 bytes: 0xD0 0x00 ... endcode)
    uint8_t respBuf[32];
    size_t bytesRead = 0;
    uint32_t startWait = millis();
    while (client.connected() && bytesRead < 11 && (millis() - startWait < 2000)) {
        if (client.available()) {
            respBuf[bytesRead++] = client.read();
        } else {
            delay(2);
        }
    }

    if (bytesRead < 11) {
        outError = "Write response timeout from PLC";
        client.stop();
        return false;
    }

    int writeEndCode = McProtocol::parseWriteResponse(respBuf, bytesRead);
    if (writeEndCode != 0) {
        outError = "PLC rejected write with MC error 0x" + String(writeEndCode, HEX);
        client.stop();
        return false;
    }

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

    client.write(readFrame, readLen);
    client.flush();

    bytesRead = 0;
    startWait = millis();
    while (client.connected() && bytesRead < 12 && (millis() - startWait < 2000)) {
        if (client.available()) {
            respBuf[bytesRead++] = client.read();
        } else {
            delay(2);
        }
    }
    client.stop();

    if (bytesRead < 12) {
        outError = "Read-back response timeout from PLC";
        return false;
    }

    uint8_t readBits[2] = {0};
    int readEndCode = McProtocol::parseReadBitResponse(respBuf, bytesRead, 1, readBits);
    if (readEndCode != 0) {
        outError = "PLC rejected read-back with MC error 0x" + String(readEndCode, HEX);
        return false;
    }

    outReadBack = (int)readBits[0];

    // Read-back verification invariant
    if (outReadBack != state) {
        outError = "Read-back mismatch: Requested " + String(state) + " but read back as " + String(outReadBack);
        return false;
    }

    return true;
}
