#pragma once

#include <Arduino.h>

// MC Protocol Constants
#define MC_DEV_X  0x9C  // Input Relay
#define MC_DEV_Y  0x9D  // Output Relay
#define MC_DEV_M  0x90  // Internal Relay
#define MC_DEV_D  0xA8  // Data Register

#define MC_CMD_BATCH_READ   0x0401
#define MC_CMD_BATCH_WRITE  0x1401

#define MC_SUB_BIT_Q        0x0001
#define MC_SUB_BIT_IQR      0x0003

class McProtocol {
public:
    static size_t buildReadBitFrame(
        uint8_t devCode,
        uint32_t headAddr,
        uint16_t pointCount,
        bool isIQR,
        uint8_t* outBuf,
        size_t maxLen
    );

    static size_t buildWriteBitFrame(
        uint8_t devCode,
        uint32_t headAddr,
        uint8_t bitState,
        bool isIQR,
        uint8_t* outBuf,
        size_t maxLen
    );

    static int parseReadBitResponse(
        const uint8_t* respBuf,
        size_t respLen,
        uint16_t pointCount,
        uint8_t* outBits
    );

    static int parseWriteResponse(
        const uint8_t* respBuf,
        size_t respLen
    );
};
