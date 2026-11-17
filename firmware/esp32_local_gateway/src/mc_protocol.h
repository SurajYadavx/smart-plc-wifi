#pragma once

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include <stdint.h>
  #include <stddef.h>
  #include <stdbool.h>
#endif

// MC Protocol Constants
#define MC_DEV_X  0x9C  // Input Relay
#define MC_DEV_Y  0x9D  // Output Relay
#define MC_DEV_M  0x90  // Internal Relay
#define MC_DEV_D  0xA8  // Data Register

#define MC_CMD_BATCH_READ   0x0401
#define MC_CMD_BATCH_WRITE  0x1401

#define MC_SUB_BIT_Q        0x0001
#define MC_SUB_BIT_IQR      0x0003

// Return Error Codes for parsing (< 0 = framing/protocol errors, > 0 = PLC end code)
#define MC_ERR_INCOMPLETE_HEADER -1  // Buffer less than 11 bytes
#define MC_ERR_BAD_SUBHEADER     -2  // Subheader not 0xD0 0x00
#define MC_ERR_INCOMPLETE_DATA   -3  // Buffer less than total expected length
#define MC_ERR_INVALID_LENGTH    -4  // Data length field in header is invalid (< 2)
#define MC_ERR_DATA_TRUNCATED    -5  // Payload data shorter than requested points
#define MC_ERR_OVERFLOW          -6  // Payload exceeds buffer bounds
#define MC_ERR_FORBIDDEN_POINT   -7  // Target point is forbidden (e.g. Y0)

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

    // Extracts response data length from bytes 7-8 and computes total expected frame size
    static int getExpectedResponseLength(const uint8_t* respBuf, size_t currentLen);

    // Bounded parser for bit read response
    // Returns 0 on success, > 0 for PLC end-code error, < 0 for framing/protocol errors
    static int parseReadBitResponse(
        const uint8_t* respBuf,
        size_t respLen,
        uint16_t pointCount,
        uint8_t* outBits
    );

    // Bounded parser for write response
    // Returns 0 on success, > 0 for PLC end-code error, < 0 for framing/protocol errors
    static int parseWriteResponse(
        const uint8_t* respBuf,
        size_t respLen
    );

    // Q-Series 8-Output Address & Point Validation
    static bool isValidQOutputAddr(uint32_t headAddr);
    static int getQOutputIndex(uint32_t headAddr);
    static bool parseQOutputPoint(const char* pointName, uint32_t& outAddr);
};

#ifdef __cplusplus
extern "C" {
#endif

size_t c_buildReadBitFrame(uint8_t devCode, uint32_t headAddr, uint16_t pointCount, bool isIQR, uint8_t* outBuf, size_t maxLen);
size_t c_buildWriteBitFrame(uint8_t devCode, uint32_t headAddr, uint8_t bitState, bool isIQR, uint8_t* outBuf, size_t maxLen);
int c_getExpectedResponseLength(const uint8_t* respBuf, size_t currentLen);
int c_parseReadBitResponse(const uint8_t* respBuf, size_t respLen, uint16_t pointCount, uint8_t* outBits);
int c_parseWriteResponse(const uint8_t* respBuf, size_t respLen);
bool c_isValidQOutputAddr(uint32_t headAddr);
int c_getQOutputIndex(uint32_t headAddr);
bool c_parseQOutputPoint(const char* pointName, uint32_t* outAddr);

#ifdef __cplusplus
}
#endif
