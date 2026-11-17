"""
tests/test_mc_protocol_cpp.py
──────────────────────────────
Python ctypes binding test suite for the actual embedded C++ firmware implementation
in firmware/esp32_local_gateway/src/mc_protocol.cpp.

Verifies:
1. McProtocol::buildReadBitFrame for Q-Series (Subcommand 0x0001, 3-byte address, 1-byte code)
2. McProtocol::buildReadBitFrame for iQ-R Series (Subcommand 0x0003, 4-byte address, 2-byte code)
3. McProtocol::buildWriteBitFrame for Q-Series and iQ-R
4. Permanent Y0 write prohibition at the C++ frame builder level
5. McProtocol::getExpectedResponseLength with valid, short, and malformed headers
6. McProtocol::parseReadBitResponse with successful bit unpacking and PLC error end codes
7. McProtocol::parseWriteResponse with 0x0000 success and non-zero PLC end codes
8. Bounded rejection of truncated, malformed, and corrupted frames
"""

import os
import unittest
import ctypes

DLL_PATH = os.path.join(os.path.dirname(__file__), "mc_protocol.dll")

class TestMcProtocolCpp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(DLL_PATH):
            raise unittest.SkipTest(f"mc_protocol.dll not found at {DLL_PATH}")
        try:
            cls.dll = ctypes.CDLL(DLL_PATH)
        except OSError as e:
            raise unittest.SkipTest(f"Dynamic loading of mc_protocol.dll prevented by OS security policy: {e}")

        # Setup ctypes signatures
        cls.dll.c_buildReadBitFrame.argtypes = [
            ctypes.c_uint8, ctypes.c_uint32, ctypes.c_uint16, ctypes.c_bool,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t
        ]
        cls.dll.c_buildReadBitFrame.restype = ctypes.c_size_t

        cls.dll.c_buildWriteBitFrame.argtypes = [
            ctypes.c_uint8, ctypes.c_uint32, ctypes.c_uint8, ctypes.c_bool,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t
        ]
        cls.dll.c_buildWriteBitFrame.restype = ctypes.c_size_t

        cls.dll.c_getExpectedResponseLength.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t
        ]
        cls.dll.c_getExpectedResponseLength.restype = ctypes.c_int

        cls.dll.c_parseReadBitResponse.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.c_uint16,
            ctypes.POINTER(ctypes.c_uint8)
        ]
        cls.dll.c_parseReadBitResponse.restype = ctypes.c_int

        cls.dll.c_parseWriteResponse.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t
        ]
        cls.dll.c_parseWriteResponse.restype = ctypes.c_int

        if hasattr(cls.dll, "c_isValidQOutputAddr"):
            cls.dll.c_isValidQOutputAddr.argtypes = [ctypes.c_uint32]
            cls.dll.c_isValidQOutputAddr.restype = ctypes.c_bool

        if hasattr(cls.dll, "c_getQOutputIndex"):
            cls.dll.c_getQOutputIndex.argtypes = [ctypes.c_uint32]
            cls.dll.c_getQOutputIndex.restype = ctypes.c_int

        if hasattr(cls.dll, "c_parseQOutputPoint"):
            cls.dll.c_parseQOutputPoint.argtypes = [
                ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32)
            ]
            cls.dll.c_parseQOutputPoint.restype = ctypes.c_bool

    def test_cpp_build_read_frame_q_series(self):
        """Verify C++ McProtocol::buildReadBitFrame for Q-Series (21 bytes, sub 0x0001)."""
        buf = (ctypes.c_uint8 * 64)()
        # devCode=0x9C (X), headAddr=0x00, pointCount=8, isIQR=False
        length = self.dll.c_buildReadBitFrame(0x9C, 0x00, 8, False, buf, 64)
        self.assertEqual(length, 21)
        raw = bytes(buf[:length])
        self.assertEqual(raw[0:2], b"\x50\x00")  # 3E Subheader
        self.assertEqual(raw[7:9], b"\x0C\x00")  # Length = 12
        self.assertEqual(raw[11:13], b"\x01\x04")  # Command 0x0401
        self.assertEqual(raw[13:15], b"\x01\x00")  # Subcommand 0x0001 (Q bit)
        self.assertEqual(raw[15:18], b"\x00\x00\x00")  # 3-byte address
        self.assertEqual(raw[18], 0x9C)  # Device code
        self.assertEqual(raw[19:21], b"\x08\x00")  # 8 points

    def test_cpp_build_read_frame_iqr_series(self):
        """Verify C++ McProtocol::buildReadBitFrame for iQ-R (23 bytes, sub 0x0003, 4-byte addr)."""
        buf = (ctypes.c_uint8 * 64)()
        length = self.dll.c_buildReadBitFrame(0x9D, 0x20, 8, True, buf, 64)
        self.assertEqual(length, 23)
        raw = bytes(buf[:length])
        self.assertEqual(raw[0:2], b"\x50\x00")
        self.assertEqual(raw[7:9], b"\x0E\x00")  # Length = 14
        self.assertEqual(raw[11:13], b"\x01\x04")
        self.assertEqual(raw[13:15], b"\x03\x00")  # Subcommand 0x0003 (iQ-R bit)
        self.assertEqual(raw[15:19], b"\x20\x00\x00\x00")  # 4-byte address
        self.assertEqual(raw[19:21], b"\x9D\x00")  # 2-byte device code
        self.assertEqual(raw[21:23], b"\x08\x00")  # 8 points

    def test_cpp_build_write_frame_q_series(self):
        """Verify C++ McProtocol::buildWriteBitFrame for Q-Series (22 bytes, high-nibble bit)."""
        buf = (ctypes.c_uint8 * 64)()
        # Write Y20 (0x20) ON (1)
        length = self.dll.c_buildWriteBitFrame(0x9D, 0x20, 1, False, buf, 64)
        self.assertEqual(length, 22)
        raw = bytes(buf[:length])
        self.assertEqual(raw[11:13], b"\x01\x14")  # Command 0x1401
        self.assertEqual(raw[13:15], b"\x01\x00")  # Subcommand 0x0001
        self.assertEqual(raw[15:18], b"\x20\x00\x00")  # 3-byte address
        self.assertEqual(raw[18], 0x9D)
        self.assertEqual(raw[19:21], b"\x01\x00")  # 1 point
        self.assertEqual(raw[21], 0x10)  # High nibble ON

        # Write Y20 OFF (0)
        length_off = self.dll.c_buildWriteBitFrame(0x9D, 0x20, 0, False, buf, 64)
        self.assertEqual(length_off, 22)
        raw_off = bytes(buf[:length_off])
        self.assertEqual(raw_off[21], 0x00)  # High nibble OFF

    def test_cpp_build_write_frame_iqr_series(self):
        """Verify C++ McProtocol::buildWriteBitFrame for iQ-R (24 bytes)."""
        buf = (ctypes.c_uint8 * 64)()
        length = self.dll.c_buildWriteBitFrame(0x9D, 0x20, 1, True, buf, 64)
        self.assertEqual(length, 24)
        raw = bytes(buf[:length])
        self.assertEqual(raw[11:13], b"\x01\x14")
        self.assertEqual(raw[13:15], b"\x03\x00")  # Subcommand 0x0003
        self.assertEqual(raw[15:19], b"\x20\x00\x00\x00")  # 4-byte address
        self.assertEqual(raw[19:21], b"\x9D\x00")
        self.assertEqual(raw[23], 0x10)

    def test_cpp_banned_y0_is_rejected_at_frame_builder(self):
        """P0 SAFETY: McProtocol::buildWriteBitFrame must refuse to build write frame for Y0."""
        buf = (ctypes.c_uint8 * 64)()
        # Q-Series write to Y0 (0x9D, addr 0x00)
        len_q = self.dll.c_buildWriteBitFrame(0x9D, 0x00, 1, False, buf, 64)
        self.assertEqual(len_q, 0, "C++ builder must strictly refuse to construct Y0 write frame for Q-Series")

        # iQ-R write to Y0
        len_r = self.dll.c_buildWriteBitFrame(0x9D, 0x00, 1, True, buf, 64)
        self.assertEqual(len_r, 0, "C++ builder must strictly refuse to construct Y0 write frame for iQ-R Series")

    def test_cpp_invalid_state_rejected_in_frame_builder(self):
        """McProtocol::buildWriteBitFrame rejects state > 1."""
        buf = (ctypes.c_uint8 * 64)()
        self.assertEqual(self.dll.c_buildWriteBitFrame(0x9D, 0x20, 2, False, buf, 64), 0)
        self.assertEqual(self.dll.c_buildWriteBitFrame(0x9D, 0x20, 255, False, buf, 64), 0)

    def test_cpp_get_expected_response_length(self):
        """Verify McProtocol::getExpectedResponseLength computes 9 + dataLen accurately."""
        # 8-point read response: dataLen = 6 -> total = 15
        header_15 = bytes([0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00])
        buf_15 = (ctypes.c_uint8 * len(header_15))(*header_15)
        self.assertEqual(self.dll.c_getExpectedResponseLength(buf_15, len(header_15)), 15)

        # Write response: dataLen = 2 -> total = 11
        header_11 = bytes([0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00])
        buf_11 = (ctypes.c_uint8 * len(header_11))(*header_11)
        self.assertEqual(self.dll.c_getExpectedResponseLength(buf_11, len(header_11)), 11)

        # Header too short (< 9 bytes)
        self.assertEqual(self.dll.c_getExpectedResponseLength(buf_15, 8), -1)

        # Bad subheader
        bad_sub = bytes([0x50, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x06, 0x00])
        buf_bad = (ctypes.c_uint8 * len(bad_sub))(*bad_sub)
        self.assertEqual(self.dll.c_getExpectedResponseLength(buf_bad, len(bad_sub)), -2)

    def test_cpp_parse_read_bit_response_success(self):
        """Verify McProtocol::parseReadBitResponse unpacks 8 bits correctly."""
        # Frame: 0xD0 0x00 ... dataLen=6, endcode=0x0000, bits
        # Bit packing: byte 11: X0=0 (high), X1=1 (low) -> 0x01
        # byte 12: X2=0 (high), X3=1 (low) -> 0x01
        frame = bytes([
            0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
            0x06, 0x00,  # data length = 6
            0x00, 0x00,  # end code = 0x0000 (success)
            0x01,        # X0=0, X1=1
            0x01,        # X2=0, X3=1
            0x00,        # X4=0, X5=0
            0x00         # X6=0, X7=0
        ])
        buf = (ctypes.c_uint8 * len(frame))(*frame)
        out_bits = (ctypes.c_uint8 * 8)()
        res = self.dll.c_parseReadBitResponse(buf, len(frame), 8, out_bits)
        self.assertEqual(res, 0)
        self.assertEqual(list(out_bits), [0, 1, 0, 1, 0, 0, 0, 0])

    def test_cpp_parse_read_bit_response_plc_end_code_error(self):
        """Verify McProtocol::parseReadBitResponse returns actual PLC error end code (e.g. 0xC059)."""
        err_frame = bytes([
            0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
            0x02, 0x00,  # data length = 2 (endcode only)
            0x59, 0xC0   # end code = 0xC059
        ])
        buf = (ctypes.c_uint8 * len(err_frame))(*err_frame)
        out_bits = (ctypes.c_uint8 * 8)()
        res = self.dll.c_parseReadBitResponse(buf, len(err_frame), 8, out_bits)
        self.assertEqual(res, 0xC059)

    def test_cpp_parse_write_response_lifecycle(self):
        """Verify McProtocol::parseWriteResponse for success and write-protect errors."""
        ok_frame = bytes([
            0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
            0x02, 0x00,
            0x00, 0x00
        ])
        buf_ok = (ctypes.c_uint8 * len(ok_frame))(*ok_frame)
        self.assertEqual(self.dll.c_parseWriteResponse(buf_ok, len(ok_frame)), 0)

        err_frame = bytes([
            0xD0, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x00,
            0x02, 0x00,
            0x5B, 0xC0  # 0xC05B write protection error
        ])
        buf_err = (ctypes.c_uint8 * len(err_frame))(*err_frame)
        self.assertEqual(self.dll.c_parseWriteResponse(buf_err, len(err_frame)), 0xC05B)

    def test_cpp_all_eight_output_write_frames(self):
        """V2.2: Verify C++ McProtocol::buildWriteBitFrame for all 8 outputs Y20 to Y27 (hex 0x20 to 0x27)."""
        buf = (ctypes.c_uint8 * 64)()
        for i in range(8):
            addr = 0x20 + i
            length = self.dll.c_buildWriteBitFrame(0x9D, addr, 1, False, buf, 64)
            self.assertEqual(length, 22, f"Write frame for addr 0x{addr:02X} must be 22 bytes")
            raw = bytes(buf[:length])
            self.assertEqual(raw[15], addr, f"Address byte 15 must be 0x{addr:02X}")
            self.assertEqual(raw[18], 0x9D, "Device code must be 0x9D (Y)")
            self.assertEqual(raw[21], 0x10, "State 1 must be encoded as 0x10")

            # OFF state
            length_off = self.dll.c_buildWriteBitFrame(0x9D, addr, 0, False, buf, 64)
            self.assertEqual(length_off, 22)
            raw_off = bytes(buf[:length_off])
            self.assertEqual(raw_off[21], 0x00, "State 0 must be encoded as 0x00")

    def test_cpp_point_validation_helpers(self):
        """V2.2: Verify C++ address validation, index mapping, and point parsing."""
        if not hasattr(self.dll, "c_isValidQOutputAddr"):
            self.skipTest("c_isValidQOutputAddr not exported in current binary DLL")
        # 1. Address bounds: 0x20 through 0x27 must be valid
        for i in range(8):
            addr = 0x20 + i
            self.assertTrue(self.dll.c_isValidQOutputAddr(addr))
            self.assertEqual(self.dll.c_getQOutputIndex(addr), i)

        # Invalid addresses
        self.assertFalse(self.dll.c_isValidQOutputAddr(0x00))  # Y0
        self.assertFalse(self.dll.c_isValidQOutputAddr(0x1F))  # Y1F
        self.assertFalse(self.dll.c_isValidQOutputAddr(0x28))  # Y28
        self.assertEqual(self.dll.c_getQOutputIndex(0x00), -1)
        self.assertEqual(self.dll.c_getQOutputIndex(0x28), -1)

        # 2. Point name parsing
        out_addr = ctypes.c_uint32(0)
        for i in range(8):
            pt_name = f"Y2{i}".encode("ascii")
            ok = self.dll.c_parseQOutputPoint(pt_name, ctypes.byref(out_addr))
            self.assertTrue(ok, f"Point {pt_name.decode()} should parse successfully")
            self.assertEqual(out_addr.value, 0x20 + i)

            # Lowercase variant
            pt_lower = f"y2{i}".encode("ascii")
            ok_lower = self.dll.c_parseQOutputPoint(pt_lower, ctypes.byref(out_addr))
            self.assertTrue(ok_lower)
            self.assertEqual(out_addr.value, 0x20 + i)

        # Banned / invalid points
        for bad_pt in (b"Y0", b"Y00", b"Y000", b"X0", b"X3", b"Y19", b"Y28", b"D100", b"", b"INVALID"):
            ok = self.dll.c_parseQOutputPoint(bad_pt, ctypes.byref(out_addr))
            self.assertFalse(ok, f"Point {bad_pt.decode(errors='ignore')!r} must be rejected")

if __name__ == "__main__":
    unittest.main()
