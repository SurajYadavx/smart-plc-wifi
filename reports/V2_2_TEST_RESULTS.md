# PROJECT V2.2 — COMPREHENSIVE TEST RESULTS & EMPIRICAL VERIFICATION REPORT
## MITSUBISHI Q/R SERIES DUAL-PLC LOCAL SUPERVISORY AND CONTROL SYSTEM

---

## 1. Executive Test Overview

This document provides the authoritative empirical evidence for **Project V2.2**. All automated tests have been executed directly against the actual codebase in the working repository `mitsubishi-q-plc-app_LOCAL_DUAL_PLC` on branch `feature/esp32-local-dual-plc`.

### Verification Summary
- **Firmware Compilation (PlatformIO):** ✅ SUCCESS (RAM: 14.0%, Flash: 66.9%)
- **LittleFS Filesystem Generation:** ✅ SUCCESS (283,770 bytes / 1,441,792 bytes partition)
- **Python Unittest Suite:** ✅ 172 Ran, 171 PASSED, 0 FAILED, 1 SKIPPED (Hardware serial)
- **C++ Protocol ctypes Suite:** ✅ 12 Ran, 11 PASSED, 0 FAILED, 1 SKIPPED (Unexported C symbol)
- **Browser DOM & Simulation Suite:** ✅ 154 Ran, 154 PASSED, 0 FAILED, 0 SKIPPED

**Total Automated Tests:** **338 verification points across all harnesses — ZERO FAILURES.**

---

## 2. Firmware Compilation & Memory Budget (PlatformIO)

**Command:**
```powershell
.venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway
```

**Environment:**
- **Platform:** Espressif 32 (`espressif32 @ ^6.5.0`, framework: Arduino)
- **Board:** ESP32 Dev Module (`esp32dev`)
- **Hardware:** ESP32 240MHz, 320KB RAM, 4MB Flash
- **Toolchain:** `xtensa-esp32-elf-gcc @ 8.4.0`

**Empirical Build Metrics:**
```text
RAM:   [=         ]  14.0% (used 45,904 bytes from 327,680 bytes)
Flash: [=======   ]  66.9% (used 876,737 bytes from 1,310,720 bytes)
========================= [SUCCESS] Took 3.62 seconds =========================
```

### LittleFS Filesystem Partition Build
**Command:**
```powershell
.venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t buildfs
```

**Output:**
```text
Building FS image from 'data' directory to .pio\build\esp32dev\littlefs.bin
/assets/css/style.css
/assets/js/app.js
/index.html
========================= [SUCCESS] Took 1.50 seconds =========================
```
- **Generated Image Size:** `1,441,792 bytes` (`littlefs.bin`)
- **Data Directory Contents:**
  - `index.html`: 80,116 bytes
  - `assets/css/style.css`: 49,698 bytes
  - `assets/js/app.js`: 153,956 bytes
  - **Total Production Filesystem Payload:** `283,770 bytes` (19.7% of available partition space)
  - **Exclusion Compliance:** `test.html` strictly excluded from LittleFS partition.

---

## 3. Python Unit & Integration Test Suite

**Command:**
```powershell
.venv\Scripts\python.exe -m unittest discover -s tests -v
```

**Results:** `Ran 172 tests in 3.697s — OK (skipped=1)`

### Key V2.2 Test Cases:
1. `test_q_series_all_eight_outputs_frame_structure`:
   - Validates that binary write frames (`0x1401`, subcommand `0x0001`, device code `0x9D`) are generated with correct hexadecimal addresses `0x20` through `0x27`.
   - Result: **PASS**
2. `test_mock_server_all_eight_outputs_independent_writes`:
   - Spins up a real TCP mock server, connects `McControlAdapter`, and independently commands `Y20`, `Y21`, `Y22`, `Y23`, `Y24`, `Y25`, `Y26`, and `Y27`.
   - Validates read-back verification for each point without cross-talk.
   - Result: **PASS**
3. `test_simulation_no_ladder_coupling`:
   - Verifies that `SimulatedPlcAdapter` rotates input points `X0` to `X7` without driving output `Y20`.
   - Result: **PASS**
4. `test_all_eight_outputs_fastapi_endpoints`:
   - Tests REST API POST endpoints `/api/v1/devices/melsec_q03/outputs/{point}/command` for all 8 outputs.
   - Result: **PASS**
5. `test_banned_y0_cannot_reach_frame_builder`:
   - Confirms that any attempt to construct an MC protocol frame for `Y0` raises `ForbiddenPointError`.
   - Result: **PASS**
6. `test_fail_closed_physical_writes_gate`:
   - Confirms that when `writes_enabled=False` (default), write requests are rejected immediately.
   - Result: **PASS**
7. `test_invalid_pin_is_rejected`:
   - Confirms that invalid, blank, or placeholder PINs return HTTP 401 Unauthorized.
   - Result: **PASS**

---

## 4. C++ Protocol Library Verification (ctypes)

**Command:**
```powershell
.venv\Scripts\python.exe -m unittest tests/test_mc_protocol_cpp.py -v
```

**Results:** `Ran 12 tests in 0.001s — OK (skipped=1)`

| Test Name | Description | Status |
|---|---|---|
| `test_cpp_all_eight_output_write_frames` | Verifies C++ `McProtocol::buildWriteBitFrame` for all 8 points `Y20` to `Y27` (`0x20` to `0x27`) | ✅ PASS |
| `test_cpp_banned_y0_is_rejected_at_frame_builder` | Confirms frame builder rejects `0x00` (`Y0`) with error code | ✅ PASS |
| `test_cpp_build_read_frame_iqr_series` | Verifies iQ-R read frame (23 bytes, subcommand `0x0003`, 4-byte address) | ✅ PASS |
| `test_cpp_build_read_frame_q_series` | Verifies Q-Series read frame (21 bytes, subcommand `0x0001`, 3-byte address) | ✅ PASS |
| `test_cpp_build_write_frame_iqr_series` | Verifies iQ-R write frame (24 bytes) | ✅ PASS |
| `test_cpp_build_write_frame_q_series` | Verifies Q-Series write frame (22 bytes, high-nibble bit packing) | ✅ PASS |
| `test_cpp_get_expected_response_length` | Verifies response frame length calculations | ✅ PASS |
| `test_cpp_invalid_state_rejected_in_frame_builder` | Rejects bit states > 1 | ✅ PASS |
| `test_cpp_parse_read_bit_response_plc_end_code_error` | Handles PLC error end codes (e.g. `0xC059`) | ✅ PASS |
| `test_cpp_parse_read_bit_response_success` | Correctly unpacks bit arrays from PLC responses | ✅ PASS |
| `test_cpp_parse_write_response_lifecycle` | Validates write response header and end codes | ✅ PASS |
| `test_cpp_point_validation_helpers` | Address boundary testing | ⏭️ SKIP |

---

## 5. Browser DOM & Simulation Test Suite

**Command:**
```powershell
node tests/run_browser_tests.js
```

**Results:** `[Browser Tests] Result: 154 PASS, 0 FAIL, 0 SKIP`

### Coverage Breakdown across 18 Test Suites:
- **Suite 1: Telemetry Parsing & Normalization** (12 tests) — Validates bit states `0` and `1`, null rejection, and timestamps.
- **Suite 2: State Transitions & Event Logging** (10 tests) — Verifies transition event emission without false toggles.
- **Suite 3: Duplicate & Out-of-Order Protection** (8 tests) — Rejects duplicate sequence numbers and stale packets.
- **Suite 4: Station Switching & Multi-PLC Isolation** (10 tests) — Verifies UI isolation between Station 1 (Q03) and Station 2 (R04).
- **Suite 5: 5-Stage Output Control Pipeline** (14 tests) — Verifies pre-flight validation, PIN authorization, and read-back states.
- **Suite 6: Theme Management** (6 tests) — Low-glare dark mode and high-contrast light mode switching.
- **Suite 7: Demo Simulation Engine** (12 tests) — Tests normal, delayed, sequence gap, stale, and fault scenarios.
- **Suite 8: Data Age & Latency Metrics** (8 tests) — LIVE, DELAYED, and STALE threshold logic.
- **Suite 9: CSV Export & History Management** (8 tests) — Event journal filtering, search, and CSV generation.
- **Suite 10: Fail-Closed Security Invariants** (10 tests) — Y0 rejection, blank PIN failure, station 2 lockout.
- **Suite 11: System Diagnostics & Memory Reporting** (8 tests) — Frame counters, error rates, and ESP32 memory metrics.
- **Suite 12: Dual-Bank Overview & Point Counters** (8 tests) — Monitored inputs and independent output display.
- **Suite 13: Local Agent & Hardware Bridge Integration** (8 tests) — REST polling and status indicators.
- **Suite 14: Q-Series Hex Addressing & Output Catalog** (8 tests) — Validates offsets `0x20`–`0x27` for `Y20`–`Y27`.
- **Suite 15: Error Handling & Resilience** (6 tests) — Graceful recovery from malformed payloads.
- **Suite 16: Read-Back Verification Timing** (6 tests) — Timeout and retry logic.
- **Suite 17: Mobile Touch Target Usability** (4 tests) — Button sizing, spacing, and accessibility.
- **Suite 18: Independent Digital I/O Architecture & 8-Output Control (V2.2)** (8 tests):
  - Permitted output allowlist contains all 8 outputs `Y20` to `Y27`.
  - `Y0` permanently excluded from control catalog.
  - All 8 outputs maintain independent states in simulated state table.
  - Inputs `X0`–`X7` cycle independently without coupling to `Y20`.
  - Read-back indicator formatting and status chips.
  - Station 1 mode badge indicates fail-closed safety state.

---

## 6. Conclusion

The testing protocol confirms that all Project V2.2 requirements have been implemented with zero regressions. All test suites pass cleanly across firmware, backend, protocol, and frontend tiers.
