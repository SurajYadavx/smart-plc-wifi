#include <iostream>
#include <cassert>
#include <cstring>
#include "../firmware/esp32_local_gateway/src/mc_protocol.h"

int passedCount = 0;
int failedCount = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            failedCount++; \
        } else { \
            passedCount++; \
        } \
    } while (0)

void test_build_read_frame_q() {
    uint8_t buf[64];
    size_t len = McProtocol::buildReadBitFrame(MC_DEV_X, 0x00, 8, false, buf, sizeof(buf));
    TEST_ASSERT(len == 21, "Q read frame must be exactly 21 bytes");
    TEST_ASSERT(buf[0] == 0x50 && buf[1] == 0x00, "Subheader must be 0x50 0x00");
    TEST_ASSERT(buf[7] == 12 && buf[8] == 0, "Data length must be 12 (0x000C)");
    TEST_ASSERT(buf[11] == 0x01 && buf[12] == 0x04, "Command must be 0x0401 (Batch Read)");
    TEST_ASSERT(buf[13] == 0x01 && buf[14] == 0x00, "Subcommand must be 0x0001 (Bit Units Q)");
    TEST_ASSERT(buf[15] == 0x00 && buf[16] == 0x00 && buf[17] == 0x00, "Head address must be 3 bytes 0x000000");
    TEST_ASSERT(buf[18] == MC_DEV_X, "Device code must be 0x9C (X)");
    TEST_ASSERT(buf[19] == 8 && buf[20] == 0, "Point count must be 8");
}

void test_build_read_frame_iqr() {
    uint8_t buf[64];
    size_t len = McProtocol::buildReadBitFrame(MC_DEV_Y, 0x20, 8, true, buf, sizeof(buf));
    TEST_ASSERT(len == 23, "iQ-R read frame must be exactly 23 bytes");
    TEST_ASSERT(buf[7] == 14 && buf[8] == 0, "Data length must be 14 (0x000E)");
    TEST_ASSERT(buf[13] == 0x03 && buf[14] == 0x00, "Subcommand must be 0x0003 (Bit Units iQ-R)");
    TEST_ASSERT(buf[15] == 0x20 && buf[16] == 0x00 && buf[17] == 0x00 && buf[18] == 0x00, "Head address must be 4 bytes");
    TEST_ASSERT(buf[19] == MC_DEV_Y && buf[20] == 0x00, "Device code must be 2 bytes 0x9D 0x00");
    TEST_ASSERT(buf[21] == 8 && buf[22] == 0, "Point count must be 8");
}

void test_build_write_frame_q() {
    uint8_t buf[64];
    size_t len = McProtocol::buildWriteBitFrame(MC_DEV_Y, 0x20, 1, false, buf, sizeof(buf));
    TEST_ASSERT(len == 22, "Q write frame must be exactly 22 bytes");
    TEST_ASSERT(buf[7] == 13 && buf[8] == 0, "Data length must be 13");
    TEST_ASSERT(buf[11] == 0x01 && buf[12] == 0x14, "Command must be 0x1401 (Batch Write)");
    TEST_ASSERT(buf[13] == 0x01 && buf[14] == 0x00, "Subcommand must be 0x0001 (Q)");
    TEST_ASSERT(buf[15] == 0x20 && buf[16] == 0x00 && buf[17] == 0x00, "Address 0x20");
    TEST_ASSERT(buf[18] == MC_DEV_Y, "Device code 0x9D");
    TEST_ASSERT(buf[19] == 1 && buf[20] == 0, "Points = 1");
    TEST_ASSERT(buf[21] == 0x10, "State 1 must be encoded as 0x10 in high nibble");

    // OFF state
    len = McProtocol::buildWriteBitFrame(MC_DEV_Y, 0x20, 0, false, buf, sizeof(buf));
    TEST_ASSERT(len == 22, "Q write OFF frame length");
    TEST_ASSERT(buf[21] == 0x00, "State 0 must be encoded as 0x00");
}

void test_build_write_frame_iqr() {
    uint8_t buf[64];
    size_t len = McProtocol::buildWriteBitFrame(MC_DEV_Y, 0x20, 1, true, buf, sizeof(buf));
    TEST_ASSERT(len == 24, "iQ-R write frame must be exactly 24 bytes");
    TEST_ASSERT(buf[7] == 15 && buf[8] == 0, "Data length must be 15");
    TEST_ASSERT(buf[13] == 0x03 && buf[14] == 0x00, "Subcommand must be 0x0003");
    TEST_ASSERT(buf[21] == 1 && buf[22] == 0, "Points = 1");
    TEST_ASSERT(buf[23] == 0x10, "Data byte at index 23 must be 0x10");
}

void test_y0_write_prohibition() {
    uint8_t buf[64];
    // Attempting to build write frame for Y0 on Q-Series
    size_t lenQ = McProtocol::buildWriteBitFrame(MC_DEV_Y, 0x00, 1, false, buf, sizeof(buf));
    TEST_ASSERT(lenQ == 0, "Must strictly refuse to build write frame for Y0 on Q-Series");

    // Attempting to build write frame for Y0 on iQ-R
    size_t lenR = McProtocol::buildWriteBitFrame(MC_DEV_Y, 0x00, 1, true, buf, sizeof(buf));
    TEST_ASSERT(lenR == 0, "Must strictly refuse to build write frame for Y0 on iQ-R");
}

void test_expected_response_length() {
    uint8_t validHeader[15] = {
        0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
        0x06, 0x00, // Data len = 6 (2 endcode + 4 payload)
        0x00, 0x00, 0x10, 0x01, 0x00, 0x00
    };
    int len = McProtocol::getExpectedResponseLength(validHeader, 15);
    TEST_ASSERT(len == 15, "Expected length must be 9 + 6 = 15");

    // Header too short
    TEST_ASSERT(McProtocol::getExpectedResponseLength(validHeader, 8) == MC_ERR_INCOMPLETE_HEADER, "Too short header");

    // Bad subheader
    uint8_t badSub[15];
    memcpy(badSub, validHeader, 15);
    badSub[0] = 0x50;
    TEST_ASSERT(McProtocol::getExpectedResponseLength(badSub, 15) == MC_ERR_BAD_SUBHEADER, "Bad subheader");

    // Invalid length field
    uint8_t invalidLen[15];
    memcpy(invalidLen, validHeader, 15);
    invalidLen[7] = 0x01; invalidLen[8] = 0x00; // < 2
    TEST_ASSERT(McProtocol::getExpectedResponseLength(invalidLen, 15) == MC_ERR_INVALID_LENGTH, "Invalid length < 2");
}

void test_parse_read_bit_response() {
    // 8 points: X0=0, X1=1, X2=0, X3=1, X4=0, X5=0, X6=0, X7=0
    // Byte 11 (X0, X1): X0=0 -> 0x00, X1=1 -> 0x01 -> 0x01
    // Byte 12 (X2, X3): X2=0 -> 0x00, X3=1 -> 0x10 -> 0x01? Wait:
    // Bit packing in MC protocol: even bit is high nibble (0x10), odd bit is low nibble (0x01)
    // So X0 (even) = 0x00, X1 (odd) = 0x01 -> byte 11 = 0x01
    // X2 (even) = 0x00, X3 (odd) = 0x01 -> byte 12 = 0x01
    uint8_t resp[15] = {
        0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
        0x06, 0x00, // Data len = 6
        0x00, 0x00, // End code 0x0000
        0x01,       // X0=0, X1=1
        0x01,       // X2=0, X3=1
        0x00,       // X4=0, X5=0
        0x00        // X6=0, X7=0
    };

    uint8_t bits[8] = {0};
    int res = McProtocol::parseReadBitResponse(resp, 15, 8, bits);
    TEST_ASSERT(res == 0, "Parse read response must succeed with 0");
    TEST_ASSERT(bits[0] == 0, "X0 must be 0");
    TEST_ASSERT(bits[1] == 1, "X1 must be 1");
    TEST_ASSERT(bits[2] == 0, "X2 must be 0");
    TEST_ASSERT(bits[3] == 1, "X3 must be 1");
    TEST_ASSERT(bits[4] == 0, "X4 must be 0");

    // PLC Error End Code (0xC059)
    uint8_t errResp[11] = {
        0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
        0x02, 0x00, // Data len = 2 (just the end code)
        0x59, 0xC0  // End code 0xC059
    };
    int errCode = McProtocol::parseReadBitResponse(errResp, 11, 8, bits);
    TEST_ASSERT(errCode == 0xC059, "Must return actual PLC error endcode 0xC059");

    // Incomplete data
    TEST_ASSERT(McProtocol::parseReadBitResponse(resp, 12, 8, bits) == MC_ERR_INCOMPLETE_DATA, "Fewer bytes than totalExpected");

    // Incomplete header
    TEST_ASSERT(McProtocol::parseReadBitResponse(resp, 10, 8, bits) == MC_ERR_INCOMPLETE_HEADER, "< 11 bytes");
}

void test_parse_write_response() {
    uint8_t okResp[11] = {
        0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
        0x02, 0x00, // Data len = 2
        0x00, 0x00  // End code 0x0000
    };
    int res = McProtocol::parseWriteResponse(okResp, 11);
    TEST_ASSERT(res == 0, "Write success must return 0");

    // Write protection error 0xC05B
    uint8_t errResp[11] = {
        0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
        0x02, 0x00,
        0x5B, 0xC0
    };
    int errRes = McProtocol::parseWriteResponse(errResp, 11);
    TEST_ASSERT(errRes == 0xC05B, "Write protection error must return 0xC05B");

    // Incomplete header
    TEST_ASSERT(McProtocol::parseWriteResponse(okResp, 10) == MC_ERR_INCOMPLETE_HEADER, "< 11 bytes");
}

int main() {
    std::cout << "========================================\n";
    std::cout << "RUNNING C++ MC PROTOCOL FIRMWARE TESTS\n";
    std::cout << "========================================\n";

    test_build_read_frame_q();
    test_build_read_frame_iqr();
    test_build_write_frame_q();
    test_build_write_frame_iqr();
    test_y0_write_prohibition();
    test_expected_response_length();
    test_parse_read_bit_response();
    test_parse_write_response();

    std::cout << "Tests Passed: " << passedCount << "\n";
    std::cout << "Tests Failed: " << failedCount << "\n";
    std::cout << "========================================\n";

    return (failedCount == 0) ? 0 : 1;
}
