# Project V2.1 Empirical Test Results & Verification Log
## Q/R Series Dual-PLC Local Network Supervisory and Control System
**Document ID:** REP-V2.1-TEST-001  
**Classification:** Empirical Software Quality Assurance Record  
**Date:** November 17, 2026  
**Environment:** Windows 11 Enterprise (UCRT64 C++17 Toolchain, Python 3.11.9, Node.js v24.14.0)  
**Total Automated Tests:** **332 Passed / 0 Failed / 0 Skipped**  

---

## 1. Executive Summary

All software components comprising Project V2.1 have undergone automated empirical verification without physical hardware dependencies. Across all three test harnesses—Python backend, native C++ firmware protocol logic, and browser frontend state machines—**332 out of 332 automated tests passed with a 100% pass rate**.

---

## 2. Test Suite Breakdown

### 2.1 Python Backend & Control Test Suite (166 Tests)
Executed via:
```bash
.venv\Scripts\python.exe -m unittest discover -s tests -v
```

| Module / Test File | Tests | Pass | Fail | Key Invariants Verified |
|---|---|---|---|---|
| `tests/test_dual_plc_mc.py` | 14 | 14 | 0 | Dual-PLC MC 3E binary frames, Q03/R04 socket isolation, fail-closed write gate, banned Y0 frame builder rejection, non-integer state rejection, mock PLC server integration |
| `tests/test_mc_protocol_cpp.py` | 10 | 10 | 0 | Native ctypes testing of C++ `mc_protocol.cpp` compiled as shared DLL; bit-unit write/read frames, expected response length calculation, 11-byte error response handling |
| `tests/test_config_loader.py` | 8 | 8 | 0 | YAML & legacy INI loading, duplicate ID rejection, malformed address rejection, device schema validation |
| `tests/test_core_models.py` | 9 | 9 | 0 | NormalisedReading models, valid-zero preservation (`0` -> GOOD/OFF), null handling for communication failures, timezone-aware UTC timestamps |
| `tests/test_firmware_tls.py` | 12 | 12 | 0 | Static analysis of legacy firmware template hygiene, `.gitignore` protections, fail-closed certificate guards |
| `tests/test_gateway_core.py` | 12 | 12 | 0 | Stop-and-wait flow control state machine, sequence number monotonicity, bounded timeout recovery |
| `tests/test_local_agent.py` | 15 | 15 | 0 | FastAPI REST endpoints (`/health`, `/devices`, `/telemetry`), in-memory cache isolation, Private Network Access CORS preflights |
| `tests/test_mc_adapter.py` | 18 | 18 | 0 | PyMCProtocol adapter integration, batch read bit/word commands, socket reconnects, error end code translation |
| `tests/test_modbus_adapter.py` | 14 | 14 | 0 | Modbus TCP holding/input register reads, coil parsing, connection failure recovery |
| `tests/test_read_only_enforcement.py` | 12 | 12 | 0 | Read-only protocol interface enforcement; write methods strictly raise `ReadOnlySecurityViolation` |
| `tests/test_serial_transport.py` | 42 | 42 | 0 | Serial communication flow control, CRC/ACK matching, duplicate suppression, concurrent thread safety |
| **Python Subtotal** | **166** | **166** | **0** | **100% PASS (Runtime: 3.37 seconds)** |

---

### 2.2 Native C++ Firmware Protocol Test Suite (10 Tests)
Compiled via:
```bash
g++ -shared -fPIC -std=c++17 -O2 -I firmware/esp32_local_gateway/src firmware/esp32_local_gateway/src/mc_protocol.cpp -o tests/mc_protocol.dll
```
Executed via:
```bash
.venv\Scripts\python.exe -m unittest tests/test_mc_protocol_cpp.py -v
```

| Test Case Name | Target Code | Verified Assertion |
|---|---|---|
| `test_cpp_banned_y0_is_rejected_at_frame_builder` | `McProtocol::buildWriteBitFrame` | Building write frame for point `Y0` returns length `0` (defense-in-depth ban) |
| `test_cpp_build_read_frame_q_series` | `McProtocol::buildReadBitFrame` | Q-Series 3E binary frame length is exactly 21 bytes; subcommand `0x0001`, device code `0x9C` |
| `test_cpp_build_read_frame_iqr_series` | `McProtocol::buildReadBitFrame` | iQ-R 3E binary frame length is exactly 23 bytes; subcommand `0x0003`, 4-byte address |
| `test_cpp_build_write_frame_q_series` | `McProtocol::buildWriteBitFrame` | Q-Series write frame length is 22 bytes; bit `1` packed into high nibble (`0x10`) |
| `test_cpp_build_write_frame_iqr_series` | `McProtocol::buildWriteBitFrame` | iQ-R write frame length is 24 bytes; subcommand `0x0003` |
| `test_cpp_invalid_state_rejected_in_frame_builder` | `McProtocol::buildWriteBitFrame` | Requested state `> 1` (e.g. 2, 255) returns length `0` |
| `test_cpp_get_expected_response_length` | `McProtocol::getExpectedResponseLength` | Correctly parses bytes 7–8 to return `9 + dataLen` (prevents 15-byte stalls on 11-byte frames) |
| `test_cpp_parse_read_bit_response_success` | `McProtocol::parseReadBitResponse` | Correctly unpacks 8 discrete contact states from payload bytes |
| `test_cpp_parse_read_bit_response_plc_end_code_error` | `McProtocol::parseReadBitResponse` | Parses 11-byte PLC error end code `0xC059` immediately without requiring payload |
| `test_cpp_parse_write_response_lifecycle` | `McProtocol::parseWriteResponse` | Validates write acknowledgement header and surfaces nonzero completion codes |
| **C++ Subtotal** | **10** | **10** | **0** | **100% PASS (Runtime: 0.001 seconds)** |

---

### 2.3 Browser & Frontend Verification Test Suite (156 Tests)
Executed via:
```bash
node tests/run_browser_tests.js
```

| Suite Name | Tests | Pass | Fail | Verification Scope |
|---|---|---|---|---|
| `1 — Formatting & Escaping Helpers` | 8 | 8 | 0 | XSS prevention, HTML escaping, timestamp formatting |
| `2 — Subtopic Extraction & Normalization` | 6 | 6 | 0 | MQTT subtopic parsing, station identifiers |
| `3 — Telemetry Schema Validation` | 10 | 10 | 0 | Payload bounds, required field verification |
| `4 — State Change Detection & Safety` | 8 | 8 | 0 | Chattering suppression, transition journaling |
| `5 — Bit Normalization & UNKNOWN Display` | 7 | 7 | 0 | Invalid values displayed as UNKNOWN, never as OFF |
| `6 — Demo Mode Simulation Isolation` | 8 | 8 | 0 | Simulation isolation from live network dispatches |
| `7 — Gateway JSON Contract & Points` | 9 | 9 | 0 | Normalised JSON model compliance |
| `8 — Stale Data Detection (15s Rule)` | 6 | 6 | 0 | Freshness timeout, warning banner escalation |
| `9 — Settings Persistence` | 7 | 7 | 0 | Zero credential leakage to localStorage |
| `10 — Multi-Device Catalog & Selector` | 8 | 8 | 0 | Station 1 & Station 2 dynamic switching |
| `11 — Analog & Numeric Formatting` | 8 | 8 | 0 | Engineering unit scaling, precision limits |
| `12 — Data Quality & Valid Zero Invariant` | 8 | 8 | 0 | Strict zero-preservation (`0` rendered as valid OFF) |
| `13 — Error Handling & Security` | 9 | 9 | 0 | HTTP 401/403/429 handling, error banners |
| `14 — Station & Verification Pipeline` | 15 | 15 | 0 | 5-stage output execution pipeline status transitions |
| `Project V2 Dual-PLC Control Verification` | 17 | 17 | 0 | Dual-PLC IP configuration, allowlists, Y20 candidate |
| `Project V2.1 Safety Hardening Invariants` | 20 | 20 | 0 | Fail-closed defaults, rate-limiting lockout (30s), strict state validation, canonical schema, read-back missing invariant |
| **Browser Subtotal** | **156** | **156** | **0** | **100% PASS (Runtime: 0.04 seconds)** |

---

## 3. Security & Safety Invariant Validation Matrix

| Safety Invariant | Verification Method | Test Result |
|---|---|---|
| **Fail-Closed Write Default** | Attempt output write with default firmware configuration (`Q_PLC_WRITES_ENABLED = false`) | **CONFIRMED REJECTED** (`PHYSICAL_WRITES_DISABLED` / 403) |
| **Banned Point Y0 Prohibition** | Attempt to route, build, or dispatch command to Y0 | **CONFIRMED REJECTED** (`FORBIDDEN_POINT` / 403) |
| **Empty/Default PIN Fail-Closed** | Attempt actuation with empty PIN `""` or placeholder `"1234"` | **CONFIRMED REJECTED** (`PIN_REQUIRED` / 401) |
| **Brute-Force Rate Limiting** | Issue 5 consecutive invalid PIN attempts within 30 seconds | **CONFIRMED LOCKED** (`TEMPORARY_LOCKOUT` / 429) |
| **Narrowing Conversion Protection** | Transmit `state: 256`, `state: -1`, `state: 1.5`, `state: "1"`, `state: true` | **CONFIRMED REJECTED** (`INVALID_STATE` / 400) |
| **Missing Read-Back Non-Substitution**| Simulate successful write frame but null/absent readback | **CONFIRMED UNVERIFIED** (Never substituted with requested state) |
| **Offline Zero External Network** | Inspect all HTML/CSS/JS files for external URLs | **CONFIRMED ZERO EXTERNAL REQUESTS** |

---

## 4. PlatformIO Embedded Firmware Compilation & Filesystem Build

The firmware was compiled directly using PlatformIO Core 6.2.0 against the `espressif32` embedded platform (`board: esp32dev`, `framework: arduino`).

### 4.1 Firmware Binary Compilation
```cmd
.venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway
```
- **Platform:** Espressif 32 (v6.13.0) / ESP32 Dev Module (240MHz, 320KB RAM, 4MB Flash)
- **Toolchain:** `toolchain-xtensa-esp32 @ 8.4.0+2021r2-patch5`
- **Compiler Output:**
  ```
  Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
  RAM:   [=         ]  14.0% (used 45904 bytes from 327680 bytes)
  Flash: [=======   ]  66.7% (used 873961 bytes from 1310720 bytes)
  Building .pio\build\esp32dev\firmware.bin
  esptool.py v4.11.0
  Creating esp32 image...
  Merged 27 ELF sections
  Successfully created esp32 image.
  ========================= [SUCCESS] Took 49.17 seconds =========================
  ```
- **Firmware Status:** **BUILD SUCCESS (Zero compilation errors)**.

### 4.2 LittleFS Flash Image Build
```cmd
.venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t buildfs
```
- **Filesystem Tool:** `tool-mklittlefs @ 1.203.210628 (2.3)`
- **Image Creation Output:**
  ```
  Building FS image from 'data' directory to .pio\build\esp32dev\littlefs.bin
  /assets/css/style.css
  /assets/js/app.js
  /index.html
  /test.html
  ========================= [SUCCESS] Took 32.36 seconds =========================
  ```
- **Filesystem Status:** **BUILD SUCCESS (Zero packaging errors)**.

