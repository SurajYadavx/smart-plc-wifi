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
        // 4 bytes head address
        outBuf[idx++] = (uint8_t)(headAddr & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 8) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 16) & 0xFF);
        outBuf[idx++] = (uint8_t)((headAddr >> 24) & 0xFF);
        // 2 bytes device code
        outBuf[idx++] = devCode;
        outBuf[idx++] = 0x00;
    } else {
        // 3 bytes head address
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

int McProtocol::parseReadBitResponse(
    const uint8_t* respBuf,
    size_t respLen,
    uint16_t pointCount,
    uint8_t* outBits
) {
    size_t minExpected = 11 + ((pointCount + 1) / 2);
    if (respLen < minExpected) {
        return -1; // Buffer too short
    }

    // Verify subheader (0xD0 0x00)
    if (respBuf[0] != 0xD0 || respBuf[1] != 0x00) {
        return -2; // Invalid subheader
    }

    // End code at index 9, 10 (little endian)
    uint16_t endCode = respBuf[9] | (respBuf[10] << 8);
    if (endCode != 0) {
        return (int)endCode;
    }

    // Unpack bits from byte 11 onward
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
        return -1;
    }
    if (respBuf[0] != 0xD0 || respBuf[1] != 0x00) {
        return -2;
    }
    uint16_t endCode = respBuf[9] | (respBuf[10] << 8);
    return (int)endCode;
}
