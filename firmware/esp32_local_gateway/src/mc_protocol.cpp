#include "mc_protocol.h"

size_t McProtocol::buildReadBitFrame(
    uint8_t devCode,
    uint32_t headAddr,
    uint16_t pointCount,
    bool isIQR,
    uint8_t* outBuf,
    size_t maxLen
) {
    size_t expectedLen = isIQR ? 23 : 21;
    if (maxLen < expectedLen) return 0;

    uint16_t subcommand = isIQR ? MC_SUB_BIT_IQR : MC_SUB_BIT_Q;
    uint16_t reqDataLen = isIQR ? 14 : 12; // 2 (timer) + 2 (cmd) + 2 (sub) + addr + devcode + 2 (points)

    size_t idx = 0;
    // 3E Subheader (0x50 0x00)
    outBuf[idx++] = 0x50;
    outBuf[idx++] = 0x00;
    outBuf[idx++] = 0x00; // Network No
    outBuf[idx++] = 0xFF; // PC No
    outBuf[idx++] = 0xFF; // Dest Module I/O low
    outBuf[idx++] = 0x03; // Dest Module I/O high
    outBuf[idx++] = 0x00; // Dest Module Station

    // Request data length (little endian)
    outBuf[idx++] = (uint8_t)(reqDataLen & 0xFF);
    outBuf[idx++] = (uint8_t)((reqDataLen >> 8) & 0xFF);

    // CPU Timer: 16 * 250ms = 4s (little endian)
    outBuf[idx++] = 0x10;
    outBuf[idx++] = 0x00;

    // Command: 0x0401 (little endian)
    outBuf[idx++] = 0x01;
    outBuf[idx++] = 0x04;

    // Subcommand (little endian)
    outBuf[idx++] = (uint8_t)(subcommand & 0xFF);
    outBuf[idx++] = (uint8_t)((subcommand >> 8) & 0xFF);

    if (isIQR) {
        // 4 bytes head address (little endian)
        outBuf[idx++] = (uint8_t)(headAddr & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 16) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 24) & 0xFF);
        // 2 bytes device code
        outBuf[idx++] = devCode;
        outBuf[idx++] = 0x00;
    } else {
        // 3 bytes head address (little endian)
        outBuf[idx++] = (uint8_t)(headAddr & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 16) & 0xFF);
        // 1 byte device code
        outBuf[idx++] = devCode;
    }

    // Points count (little endian)
    outBuf[idx++] = (uint8_t)(pointCount & 0xFF);
    outBuf[idx++] = (uint8_t)((pointCount >> 8) & 0xFF);

    return idx;
}

size_t McProtocol::buildWriteBitFrame(
    uint8_t devCode,
    uint32_t headAddr,
    uint8_t bitState,
    bool isIQR,
    uint8_t* outBuf,
    size_t maxLen
) {
    // Defense-in-depth safety: Strict permanent refusal to build write frame for Y0
    if (devCode == MC_DEV_Y && headAddr == 0) {
        return 0; // Rejected by safety policy
    }

    // State must strictly be 0 or 1
    if (bitState > 1) {
        return 0;
    }

    size_t expectedLen = isIQR ? 24 : 22;
    if (maxLen < expectedLen) return 0;

    uint16_t subcommand = isIQR ? MC_SUB_BIT_IQR : MC_SUB_BIT_Q;
    // reqDataLen = 2(timer) + 2(cmd) + 2(sub) + addr + devcode + 2(pts) + 1(data)
    uint16_t reqDataLen = isIQR ? 15 : 13;

    size_t idx = 0;
    outBuf[idx++] = 0x50;
    outBuf[idx++] = 0x00;
    outBuf[idx++] = 0x00;
    outBuf[idx++] = 0xFF;
    outBuf[idx++] = 0xFF;
    outBuf[idx++] = 0x03;
    outBuf[idx++] = 0x00;

    outBuf[idx++] = (uint8_t)(reqDataLen & 0xFF);
    outBuf[idx++] = (uint8_t)((reqDataLen >> 8) & 0xFF);

    outBuf[idx++] = 0x10;
    outBuf[idx++] = 0x00;

    // Command: 0x1401 Batch Write (little endian)
    outBuf[idx++] = 0x01;
    outBuf[idx++] = 0x14;

    outBuf[idx++] = (uint8_t)(subcommand & 0xFF);
    outBuf[idx++] = (uint8_t)((subcommand >> 8) & 0xFF);

    if (isIQR) {
        outBuf[idx++] = (uint8_t)(headAddr & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 16) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 24) & 0xFF);
        outBuf[idx++] = devCode;
        outBuf[idx++] = 0x00;
    } else {
        outBuf[idx++] = (uint8_t)(headAddr & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 16) & 0xFF);
        outBuf[idx++] = devCode;
    }

    // Points count: 1 (little endian)
    outBuf[idx++] = 0x01;
    outBuf[idx++] = 0x00;

    // Bit value: 0x10 for 1 (ON), 0x00 for 0 (OFF)
    outBuf[idx++] = (bitState == 1) ? 0x10 : 0x00;

    return idx;
}

int McProtocol::getExpectedResponseLength(const uint8_t* respBuf, size_t currentLen) {
    if (currentLen < 9) {
        return MC_ERR_INCOMPLETE_HEADER;
    }
    if (respBuf[0] != 0xD0 || respBuf[1] != 0x00) {
        return MC_ERR_BAD_SUBHEADER;
    }
    uint16_t dataLen = (uint16_t)respBuf[7] | ((uint16_t)respBuf[8] << 8);
    if (dataLen < 2) {
        return MC_ERR_INVALID_LENGTH;
    }
    // Total frame = 9 bytes (subheader + routing + length) + dataLen
    return 9 + (int)dataLen;
}

int McProtocol::parseReadBitResponse(
    const uint8_t* respBuf,
    size_t respLen,
    uint16_t pointCount,
    uint8_t* outBits
) {
    if (respLen < 11) {
        return MC_ERR_INCOMPLETE_HEADER;
    }
    if (respBuf[0] != 0xD0 || respBuf[1] != 0x00) {
        return MC_ERR_BAD_SUBHEADER;
    }

    uint16_t dataLen = (uint16_t)respBuf[7] | ((uint16_t)respBuf[8] << 8);
    if (dataLen < 2) {
        return MC_ERR_INVALID_LENGTH;
    }

    size_t totalExpected = 9 + dataLen;
    if (respLen < totalExpected) {
        return MC_ERR_INCOMPLETE_DATA;
    }

    // End code at index 9, 10 (little endian)
    uint16_t endCode = (uint16_t)respBuf[9] | ((uint16_t)respBuf[10] << 8);
    if (endCode != 0) {
        return (int)endCode; // Return actual non-zero PLC end-code
    }

    // Success response: check payload length for pointCount bits
    size_t requiredPayloadBytes = (pointCount + 1) / 2;
    size_t actualPayloadBytes = dataLen - 2;
    if (actualPayloadBytes < requiredPayloadBytes) {
        return MC_ERR_DATA_TRUNCATED;
    }

    // Unpack bits from byte 11 onward (high nibble for even bit, low nibble for odd bit)
    for (uint16_t i = 0; i < pointCount; i++) {
        size_t byteIdx = 11 + (i / 2);
        if (i % 2 == 0) {
            outBits[i] = (respBuf[byteIdx] & 0x10) ? 1 : 0;
        } else {
            outBits[i] = (respBuf[byteIdx] & 0x01) ? 1 : 0;
        }
    }

    return 0; // Success
}

int McProtocol::parseWriteResponse(
    const uint8_t* respBuf,
    size_t respLen
) {
    if (respLen < 11) {
        return MC_ERR_INCOMPLETE_HEADER;
    }
    if (respBuf[0] != 0xD0 || respBuf[1] != 0x00) {
        return MC_ERR_BAD_SUBHEADER;
    }

    uint16_t dataLen = (uint16_t)respBuf[7] | ((uint16_t)respBuf[8] << 8);
    if (dataLen < 2) {
        return MC_ERR_INVALID_LENGTH;
    }

    size_t totalExpected = 9 + dataLen;
    if (respLen < totalExpected) {
        return MC_ERR_INCOMPLETE_DATA;
    }

    uint16_t endCode = (uint16_t)respBuf[9] | ((uint16_t)respBuf[10] << 8);
    return (int)endCode;
}

bool McProtocol::isValidQOutputAddr(uint32_t headAddr) {
    return (headAddr >= 0x20 && headAddr <= 0x27);
}

int McProtocol::getQOutputIndex(uint32_t headAddr) {
    if (headAddr >= 0x20 && headAddr <= 0x27) {
        return (int)(headAddr - 0x20);
    }
    return -1;
}

bool McProtocol::parseQOutputPoint(const char* pointName, uint32_t& outAddr) {
    if (!pointName) return false;
    while (*pointName == ' ' || *pointName == '\t') pointName++;
    if (pointName[0] != 'Y' && pointName[0] != 'y') return false;
    if (pointName[1] == '0') return false; // Strictly ban Y0, Y00, Y000
    if (pointName[1] == '2' && (pointName[2] >= '0' && pointName[2] <= '7')) {
        char next = pointName[3];
        if (next != '\0' && next != ' ' && next != '\t' && next != '\r' && next != '\n') {
            return false;
        }
        outAddr = 0x20 + (uint32_t)(pointName[2] - '0');
        return true;
    }
    return false;
}

extern "C" {
size_t c_buildReadBitFrame(uint8_t devCode, uint32_t headAddr, uint16_t pointCount, bool isIQR, uint8_t* outBuf, size_t maxLen) {
    return McProtocol::buildReadBitFrame(devCode, headAddr, pointCount, isIQR, outBuf, maxLen);
}

size_t c_buildWriteBitFrame(uint8_t devCode, uint32_t headAddr, uint8_t bitState, bool isIQR, uint8_t* outBuf, size_t maxLen) {
    return McProtocol::buildWriteBitFrame(devCode, headAddr, bitState, isIQR, outBuf, maxLen);
}

int c_getExpectedResponseLength(const uint8_t* respBuf, size_t currentLen) {
    return McProtocol::getExpectedResponseLength(respBuf, currentLen);
}

int c_parseReadBitResponse(const uint8_t* respBuf, size_t respLen, uint16_t pointCount, uint8_t* outBits) {
    return McProtocol::parseReadBitResponse(respBuf, respLen, pointCount, outBits);
}

int c_parseWriteResponse(const uint8_t* respBuf, size_t respLen) {
    return McProtocol::parseWriteResponse(respBuf, respLen);
}

bool c_isValidQOutputAddr(uint32_t headAddr) {
    return McProtocol::isValidQOutputAddr(headAddr);
}

int c_getQOutputIndex(uint32_t headAddr) {
    return McProtocol::getQOutputIndex(headAddr);
}

bool c_parseQOutputPoint(const char* pointName, uint32_t* outAddr) {
    if (!outAddr) return false;
    uint32_t addr = 0;
    bool ok = McProtocol::parseQOutputPoint(pointName, addr);
    if (ok) *outAddr = addr;
    return ok;
}
}

