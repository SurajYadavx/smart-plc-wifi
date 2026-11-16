# Project V2 Implementation Change Log & Architectural Inventory
## Complete File-by-File Record of Modifications, Additions, and Preservations

---

## 1. Inventory Summary

| Component Domain | New Files | Modified Files | Preserved Historical Files | Current Automated Tests |
|---|---|---|---|---|
| **Core Models & Adapters** | 3 | 0 | 8 | 26 unit tests |
| **Backend & Configuration** | 0 | 3 | 4 | 52 unit tests |
| **ESP32 Firmware** | 12 | 0 | 0 | Deterministic mock verified |
| **Frontend UI & State** | 0 | 4 | 4 | 148 browser assertions |
| **Automated Test Harness** | 2 | 1 | 9 | 153 Python + 7 Gateway tests |
| **Static Distribution** | 8 | 0 | 0 | Verified identical builds |
| **Engineering Docs & Reports** | 13 | 0 | 12 | Complete documentation set |
| **Total** | **38** | **8** | **37** | **308 Tests Passing (100%)** |

---

## 2. Preserved Components (Backward Compatibility Guaranteed)

In accordance with Section 2 of the Project Brief, all historical components have been preserved without destructive replacements:

- `gateway/`: Python serial communication gateway with stop-and-wait flow control protocol and unit test suite (`gateway/test_unit.py` -> 7 suites passing).
- `firmware/plc_monitor/`: Historical ESP8266 NodeMCU firmware supporting USB serial JSON ingestion, TLS 1.2, and MQTT WSS publishing.
- `adapters/modbus/`: Complete Modbus TCP client implementation and coil/register mapping (`adapters/modbus/modbus_tcp.py`).
- `core/adapter_interface.py`: Strictly read-only base adapter interface preserved to guarantee that legacy adapters retain their immutability contract.
- `core/simulated_adapter.py`: Deterministic in-memory read-only simulation adapter.

---

## 3. Newly Created Core Models & Adapters

### 3.1 `core/control_interface.py`
- **Purpose:** Abstract control adapter interface defining safety contracts for supervisory output actuation.
- **Key Types:**
  - `IPlcControlAdapter(ABC)`: Defines `execute_output_command(device_id, point_id, state, pin, readback)`.
  - `ControlCommandStatus(Enum)`: `SUCCESS`, `UNAUTHORIZED`, `FORBIDDEN_POINT`, `COMM_FAILURE`, `READBACK_MISMATCH`, `NOT_IMPLEMENTED`.
  - `OutputControlResult`: Dataclass containing status, point_id, commanded_state, readback_value, readback_verified, elapsed_ms, plc_endcode, and error message.
  - Exceptions: `ForbiddenPointError` (for Y0), `UnauthorizedControlError` (for invalid PIN).

### 3.2 `adapters/mitsubishi/mc_control.py`
- **Purpose:** Mitsubishi MC Protocol 3E Binary control adapter with write (`0x1401`) and read-back (`0x0401`) frame generation.
- **Key Logic:**
  - Hardcoded safety ban on `Y0` (`ForbiddenPointError`).
  - Device allowlist verification against configured permitted outputs.
  - Operator PIN credential validation (`OPERATOR_PIN` check).
  - Q-Series Subcommand `0x0001` (3-byte address, 1-byte device code `0x9D`).
  - iQ-R Series Subcommand `0x0003` (4-byte address, 2-byte device code `0x9D 0x00`).
  - Endcode validation (`0x0000`).
  - Immediate post-write read-back verification.

### 3.3 `core/simulated_control_adapter.py`
- **Purpose:** In-memory deterministic simulated control adapter for offline CI/CD pipelines without physical PLC hardware.
- **Key Logic:**
  - Rejects `Y0` with safety exception.
  - Enforces `Y20` allowlist for Station 1 (`melsec_q03`).
  - Enforces strict output lockout for Station 2 (`melsec_r04`).
  - Accepts operator PIN `1234`.
  - Updates internal virtual memory table and returns immediate read-back confirmation.

---

## 4. Modified Backend Services & Configuration

### 4.1 `config/devices.yaml`
- Updated `melsec_q03` connection host from historical `192.168.3.39` to working laboratory IP `192.168.3.30`.
- Added `permitted_outputs: ["Y20"]` for `melsec_q03`.
- Added Station 2 configuration: `melsec_r04` (`192.168.3.20:5010`, provisional model `R04CPU / R04ENCPU`, `permitted_outputs: []`).
- Strictly excluded `Y0` across all device definitions.

### 4.2 `agent/service.py`
- Added `get_system_info()` method returning architecture metadata, firmware version, and dual-station link status.
- Added `execute_output_command(device_id, point_id, state, pin)` with safety validation and allowlist routing.

### 4.3 `agent/api.py`
- Added `GET /api/v1/system` endpoint.
- Added conditional `POST /api/v1/devices/{device_id}/outputs/{point_id}/command` controlled by `enable_control=False` default parameter. This guarantees 100% backward compatibility with legacy read-only test suites (`test_strict_read_only_no_write_routes`).
- Extended CORS headers to support `POST` and `X-Operator-PIN`.

---

## 5. Newly Created ESP32 Gateway Firmware

Located in `firmware/esp32_local_gateway/`:
- `platformio.ini`: PlatformIO build configuration for `esp32dev`, Arduino framework, LittleFS filesystem, 115200 baud, and external libraries (`ArduinoJson 7.x`, `LiquidCrystal_I2C 1.1.x`).
- `esp32_local_gateway.ino`: Arduino IDE entry point.
- `src/secrets.example.h`: Safe template for `WIFI_SSID`, `WIFI_PASSWORD`, and `OPERATOR_PIN`.
- `src/config.h`: Central hardware definitions, network parameters (`192.168.3.30`, `192.168.3.20`), and safety allowlists.
- `src/mc_protocol.h` / `src/mc_protocol.cpp`: Binary frame serialization and deserialization for Q-Series and iQ-R Series.
- `src/plc_manager.h` / `src/plc_manager.cpp`: Non-blocking, independent dual-PLC poller engine, socket lifecycle, and immediate read-back verification.
- `src/lcd_display.h` / `src/lcd_display.cpp`: Non-blocking 16x2 I2C LiquidCrystal diagnostic display driver with auto-detection.
- `src/web_server.h` / `src/web_server.cpp`: High-throughput HTTP web server, LittleFS static file streaming, CORS, Private Network Access headers, and REST API handlers.
- `src/main.cpp`: Main setup and cooperative multitasking loop.
- `README.md`: Wiring diagrams, DC-DC buck converter safety warnings, and flashing guide.

---

## 6. Frontend HMI & Test Suite Modifications

### 6.1 `web/index.html`
- Updated brand identity to **"Q/R SERIES DUAL-PLC SUPERVISORY SYSTEM"** with backward-compatible search anchor.
- Added dual-station topology path in header (`PLC -> GATEWAY -> ROUTER -> CLIENT`).
- Added Overview Dual-Station Deck with Station 1 Card (`melsec_q03`), Station 2 Card (`melsec_r04`), and Central Embedded Gateway Card.
- Added Station Selector buttons and link status badges.
- Added Control Tab button (`tab-btn-control`).
- Added `#panel-control`: Full industrial output control panel with safety advisory banner, allowlist point selector (permitting `Y20`, excluding `Y0`), live read-back state indicator, command state toggles, operator PIN input, 5-stage verification tracker, and Station 2 safety lockout banner.

### 6.2 `web/assets/css/style.css`
- Added 350+ lines of industrial styling for `.dual-station-deck`, `.station-card`, `.control-safety-banner`, `.control-grid-layout`, `.pipeline-step`, `.btn-state`, and `.readback-live-display`.
- Mobile responsive breakpoints for smartphones and tablets.

### 6.3 `web/assets/js/app.js`
- Added DOM references and event listeners for dual-station cards and control panel.
- Enhanced `isLocalOrigin()` to recognize `192.168.x.x` subnets as local data sources.
- Added `getGatewayBaseUrl()` supporting configurable gateway endpoints.
- Implemented `selectStation(stationId)`: Synchronizes overview cards, updates control subtitles, and locks Station 2.
- Implemented `syncDualStationStatus()`: Polls `/api/v1/devices` and `/api/v1/system` to update gateway IP and LCD status.
- Implemented 5-stage verification tracker logic (`setPipelineStep`, `resetPipelineUI`).
- Implemented `handleOutputActuation()`: Enforces allowlists, rejects `Y0`, dispatches API requests, validates end-codes, and verifies physical read-back.
- Implemented `simulateControlPipeline()`: Deterministic simulation mode for offline testing.

### 6.4 `web/test.html`
- Added 10 automated unit tests for Project V2:
  - Station switching metadata
  - Valid Zero Invariant
  - Communication Failure Invariant
  - Strict Y0 prohibition
  - Permitted output allowlist validation
  - R-Series output lockout
  - Operator PIN validation
  - 5-stage pipeline transitions
  - Immediate read-back verification
  - Read-back mismatch alert handling
- **Result:** Test assertions expanded from 138 to 148 (100% PASS).

---

## 7. Newly Created Test Harnesses

### 7.1 `tests/mock_mc_server.py`
- Complete in-memory deterministic TCP server mimicking Mitsubishi MC Protocol 3E Binary format for both Q03UDECPU and iQ-R Series.
- Supports batch read (`0x0401`), batch write (`0x1401`), bit unpacking, fault injection (simulated timeouts, connection resets), and simulated execution latency.

### 7.2 `tests/test_dual_plc_mc.py`
- 11 comprehensive Python unit and integration tests:
  - Frame structure verification (Q-Series and iQ-R Series)
  - Strict Y0 prohibition enforcement
  - Operator PIN validation
  - Allowlist enforcement
  - Dual-PLC independent operation (offline R-PLC does not affect Q-PLC)
  - End-to-end write and read-back integration with Mock MC Server
  - FastAPI control endpoint lifecycle
- **Result:** Total Python test suite expanded from 142 to 153 tests (100% PASS).
