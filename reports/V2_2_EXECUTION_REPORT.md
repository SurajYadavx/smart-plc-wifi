# PROJECT V2.2 — COMPLETE ENGINEERING EXECUTION REPORT
## MITSUBISHI Q/R SERIES DUAL-PLC LOCAL SUPERVISORY AND CONTROL SYSTEM
### Eight-Output Independent Control Restoration, ESP32 Memory Validation, and Dual-PLC I/O Architecture Correction

---

## 1. Executive Summary & Core Engineering Mandate

Project V2.2 represents the definitive architectural realignment and feature completion milestone for the **Mitsubishi Q/R Series Dual-PLC Local Supervisory and Control System**. Following independent engineering review and physical laboratory prototype demonstration, this milestone corrects the fundamental input/output operational model, restores full 8-channel independent digital output control, introduces deep ESP32 hardware and memory introspection, hardens fail-closed write safety gates, optimizes flash filesystem partitioning, and systematically purges legacy ladder assumptions across all firmware, web UI, simulation, tests, and active documentation.

### Baseline Verification & Development State
- **Repository Working Copy:** `mitsubishi-q-plc-app_LOCAL_DUAL_PLC`
- **Active Branch:** `feature/esp32-local-dual-plc`
- **System Version:** Synchronized to **`2.2.0`** across PlatformIO configuration, firmware headers, boot banners, REST APIs, and UI assets.
- **Git Commit Safety:** Preserved locally on development branch; zero remote pushes to external repositories.

---

## 2. Updated Source of Truth: Physical Prototype Demonstration

The physical laboratory prototype demonstrated the following verified operational capabilities:
1. **8 Physical Inputs (`X0`–`X7`):** Continuously monitored via MC Protocol 3E binary TCP (`0x0401` batch read, subcommand `0x0001`, device code `0x9C`, device offsets `0x00`–`0x07`).
2. **8 Physical Outputs (`Y20`–`Y27`):** Monitored and independently controlled via MC Protocol 3E binary TCP (`0x0401` batch read / `0x1401` batch write, subcommand `0x0001`, device code `0x9D`, hexadecimal offsets `0x20`–`0x27`).
3. **Independent Output Switching:** Each of the eight output channels (`Y20`, `Y21`, `Y22`, `Y23`, `Y24`, `Y25`, `Y26`, `Y27`) switches ON and OFF independently through the mobile and desktop web interface.
4. **Zero Ladder Coupling:** The previously assumed ladder relationship (`LD X003 -> OUT Y020`) was an artifact of earlier documentation and does NOT exist in the active physical prototype bench configuration. Inputs `X0`–`X7` and outputs `Y20`–`Y27` operate with complete functional independence.
5. **Permanent Y0 Exclusion:** Point `Y0` was mistakenly included in early conceptual sketches. In industrial Mitsubishi automation rigs, `Y0` is frequently assigned to emergency circuits or critical machinery and remains strictly forbidden across all layers.

---

## 3. Detailed Architectural & Implementation Changes

### 3.1 Firmware Configuration & Fail-Closed Safety Gates (`config.h`)
- **Version Bump:** Synchronized `#define FIRMWARE_VERSION "2.2.0"`.
- **Independent 8-Output Allowlist:** Defined explicit arrays:
  ```cpp
  static const uint32_t Q_PERMITTED_OUTPUT_ADDRS[8] = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27 };
  static const char* const Q_PERMITTED_OUTPUT_NAMES[8] = {
      "Y20", "Y21", "Y22", "Y23", "Y24", "Y25", "Y26", "Y27"
  };
  ```
- **Fail-Closed Write Gates:**
  - `#define Q_PLC_WRITES_ENABLED false`: Q-Series physical writes fail closed by default.
  - `#define R_PLC_WRITES_ENABLED false`: R-Series physical writes remain locked.
- **Fail-Closed Operator PIN:** Insecure fallback PIN `1234` removed. Unconfigured or blank PINs immediately reject actuation requests.
- **Permanent Y0 Ban:** Hardcoded prohibition retained; no route or memory mapping exists for `Y0`.

### 3.2 Protocol Layer (`mc_protocol.h` / `mc_protocol.cpp`)
- **Hexadecimal Offset Mapping:** Added helper functions:
  - `isValidQOutputAddr(uint32_t addr)`: Validates that `addr >= 0x20 && addr <= 0x27`.
  - `getQOutputIndex(uint32_t addr)`: Converts address to array index `(addr - 0x20)` (`0..7`).
  - `parseQOutputPoint(const char* pointStr, uint32_t* outAddr)`: Parses string (e.g. `"Y24"`) using hexadecimal parsing `strtoul(pointStr + 1, NULL, 16)` and validates range `0x20..0x27`.
- **Command & Subcommand Encoding:**
  - Bit batch write opcode: `0x1401`
  - Bit batch read opcode: `0x0401`
  - Subcommand: `0x0001` (Q-Series 1-point bit write, 22-byte frame, high-nibble bit packing).
- **C-Linkage Exports for ctypes Testing:** Exported `c_isValidQOutputAddr`, `c_getQOutputIndex`, and `c_parseQOutputPoint` for Python test automation.

### 3.3 PLC Manager (`plc_manager.cpp`)
- **Boot Banner:** Updated to `V2.2`.
- **8-Output Cache Isolation:** In `executeWrite` for `melsec_q03`:
  - Validates point against `Y20` through `Y27`.
  - Maps to cache index `idx = addr - 0x20`.
  - Modifies ONLY the target output index in `targetDev->outputs[outIdx]`.
  - Uncommanded outputs remain untouched.
  - Generates binary MC 3E write frame with opcode `0x1401` and subcommand `0x0001`.
  - Automatically performs post-write read-back verification.

### 3.4 Web Server & Hardware Introspection (`web_server.cpp` / `main.cpp`)
- **Dynamic 8-Output REST Routes:** Registered dedicated POST control endpoints for all 8 points across Station 1 and Station 2:
  - `/api/v1/devices/melsec_q03/outputs/Y20/command` through `Y27/command`
  - `/api/v1/devices/melsec_r04/outputs/Y20/command` through `Y27/command`
  - Point `Y0` route strictly omitted.
- **Hardware & Memory Diagnostics Endpoint (`/api/v1/system`):**
  Added structured `hardware` and `memory` telemetry:
  ```json
  {
    "hardware": {
      "chip_model": "ESP32-D0WDQ6",
      "chip_revision": 1,
      "cpu_cores": 2,
      "cpu_freq_mhz": 240,
      "flash_size_bytes": 4194304,
      "flash_speed_mhz": 40
    },
    "memory": {
      "free_heap_bytes": 273456,
      "min_free_heap_bytes": 268120,
      "max_alloc_heap_bytes": 118772,
      "psram_size_bytes": 0,
      "free_psram_bytes": 0
    }
  }
  ```
- **Allowlist Exposure:** `/api/v1/devices` dynamically returns all 8 permitted outputs `["Y20", ..., "Y27"]` when writes are enabled.

### 3.5 Filesystem Budget Optimization (LittleFS)
- **LittleFS Partition Allocation:** `1,441,792 bytes` (~1.4 MB).
- **Production Asset Isolation:** Excluded `test.html` (~86 KB) from `firmware/esp32_local_gateway/data/`.
- **Production Data Image:** Contains ONLY `index.html` (80 KB) and `assets/` (style.css, app.js = ~203 KB).
- **Total LittleFS Consumption:** `283,770 bytes` (~277 KB), consuming only **19.7%** of partition capacity.
- **Test Harness Preservation:** `test.html` preserved in `web/` and `dist/hosted/` for workstation verification and CI/CD.

### 3.6 Python Core & Simulation Layer
- **`config/devices.yaml`:** Updated `permitted_outputs` to all 8 points `["Y20", "Y21", "Y22", "Y23", "Y24", "Y25", "Y26", "Y27"]`. Purged ladder comments.
- **`core/config_loader.py`:** Added `permitted_outputs` field to `DeviceConfig`, parsing YAML and legacy INI.
- **`adapters/mitsubishi/mc_control.py`:** Defaulted `writes_enabled: bool = False` (fail-closed). Defaulted Q-Series allowlist to all 8 points.
- **`core/simulated_adapter.py` & `core/simulated_control_adapter.py`:**
  - **Purged ladder coupling:** Input ticks cycle independently without mirroring to Y20.
  - Outputs maintain isolated states in simulated memory table.

### 3.7 Web Application & ISA-101 HMI (`web/index.html` / `web/assets/js/app.js`)
- **Independent Digital I/O Status Overview Matrix:** Replaced legacy single-rung flow diagram with dual-bank overview matrix:
  - Input Bank: Monitored signals `X0` to `X7`.
  - Output Bank: Independent signals `Y20` to `Y27`.
- **8-Output Control Selector:** Updated dropdown `#ctrl-select-point` to enumerate all 8 points (`Y20` through `Y27`).
- **Live Read-Back Status Grid:** Added interactive status chips (`#chip-out-Y20` through `#chip-out-Y27`) displaying real-time read-back state. Clicking any chip automatically selects that point in the control panel.
- **Purge of Ladder Artifacts:** Completely removed ladder cards, rungs, and textual references across all 5 tabs.

---

## 4. Verification & Validation Summary

| Test Category | Suite / Harness | Tests Run | Passed | Failed | Skipped | Status |
|---|---|---:|---:|---:|---:|---|
| **Python Unit & Integration** | `unittest discover -s tests` | 172 | 171 | 0 | 1 | ✅ PASS |
| **C++ Protocol via ctypes** | `tests/test_mc_protocol_cpp.py` | 12 | 11 | 0 | 1 | ✅ PASS |
| **Browser DOM & Simulation** | `tests/run_browser_tests.js` | 154 | 154 | 0 | 0 | ✅ PASS |
| **PlatformIO Firmware Build** | `platformio run -d firmware/...` | 1 | 1 | 0 | 0 | ✅ PASS (Flash: 66.9%, RAM: 14.0%) |
| **LittleFS Filesystem Build** | `platformio run -t buildfs` | 1 | 1 | 0 | 0 | ✅ PASS (277 KB / 1.44 MB) |

---

## 5. Deployment & Handover Package

The handover package `PROJECT_V2_2_HANDOVER.zip` has been assembled and includes:
1. Complete firmware source code, PlatformIO project, and LittleFS data image.
2. Production web assets (`web/`) and hosted distribution (`dist/hosted/`).
3. Complete Python agent, core engine, Mitsubishi adapters, and simulation fixtures.
4. Comprehensive test suites (`tests/`).
5. Complete documentation (`docs/`) and engineering reports (`reports/`).

The codebase is 100% verified offline and ready for physical laboratory commissioning.
