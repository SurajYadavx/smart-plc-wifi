# Mitsubishi Q/R Series Dual-PLC Local Supervisory & Control System (Project V2.2)

A production-grade, local-network supervisory and authorized control application for Mitsubishi Q-Series (`Q03UDECPU`) and iQ-R Series (`R04`) PLCs, powered by an embedded ESP32 gateway.

> **PROJECT V2.2 RELEASE & FAIL-CLOSED SAFETY STATUS**  
> - **Independent 8-Channel Digital I/O:** Monitors inputs `X0–X7` (`0x9C`, `0x00..0x07`) and outputs `Y20–Y27` (`0x9D`, `0x20..0x27`). Supports independent switching of all 8 output channels `Y20` through `Y27`.
> - **Ladder Superseded Notice:** Earlier documentation referenced an `X3 -> Y20` ladder relationship. As demonstrated on the physical test bench, inputs and outputs operate independently; no ladder relationship connects `X3` to `Y20`.
> - **Fail-Closed Write Safety:** Physical writes are **disabled by default** (`Q_PLC_WRITES_ENABLED false`, `R_PLC_WRITES_ENABLED false`). Physical actuation requires explicit local commissioning configuration and valid operator PIN authentication.
> - **Permanent Y0 Prohibition:** Device `Y0` is strictly banned and rejected across all architectural layers.

## Architecture

```
[ Mitsubishi Q03UDECPU ] (Station 1: 192.168.3.30:5010, MC 3E Binary)
        │ (Ethernet)
[ TP-Link Router RUBY ] ── (Wi-Fi) ── [ ESP32 Local Gateway (V2.2 Firmware) ]
        │ (Ethernet)                                 │ (Embedded Web Server / LittleFS)
[ Mitsubishi iQ-R R04 ] (Station 2: 192.168.3.40:5010) │
                                                     ▼
                                      [ Mobile & Desktop Browser HMI ]
```

See [docs/architecture.md](docs/architecture.md) for full detail.

## Project Structure

```
mitsubishi-q-plc-app/
├── web/                          # Static GitHub Pages frontend
│   ├── index.html                # Dashboard
│   └── assets/
│       ├── css/style.css         # Industrial dark-mode stylesheet
│       └── js/app.js             # MQTT client, telemetry logic, UI
│
├── gateway/                      # Windows Python PLC gateway
│   ├── gateway.py                # Main entry point
│   ├── requirements.txt          # pymcprotocol, pyserial
│   ├── test_plc_connection.py    # Read-only smoke-test
│   ├── config/
│   │   ├── settings.ini          # PLC IP, serial port, poll interval
│   │   └── secrets.ini.example   # Template — copy, fill, do NOT commit
│   ├── src/
│   │   ├── plc_reader.py         # MC Protocol reader (read-only)
│   │   └── serial_publisher.py   # JSON serial writer
│   └── logs/                     # Runtime logs (git-ignored)
│
├── firmware/                     # NodeMCU ESP8266 Arduino firmware
│   └── plc_monitor/
│       ├── plc_monitor.ino       # Main sketch
│       └── secrets.h.example     # Template — copy, fill, do NOT commit
│
├── docs/                         # Documentation
│   ├── architecture.md           # System design, topic schema, security
│   ├── gateway-setup.md          # Python gateway setup guide
│   ├── firmware-setup.md         # Arduino / ESP8266 setup guide
│   └── deployment.md             # GitHub Pages deployment
│
├── LAB_HANDOVER.md               # Lab commissioning runbook & integration guide
├── .gitignore                    # Excludes secrets, venvs, logs
└── README.md                     # This file
```

## Lab Commissioning & Runbook

For physical lab integration and on-site hardware commissioning, refer directly to the **[Lab Handover Procedure & Integration Runbook](LAB_HANDOVER.md)** for:
- Required hardware/software checklists
- Lab configuration checklist (PLC IP, COM port, EMQX Cloud broker)
- Exact sequential execution steps (PLC smoke-test → serial verification → firmware flashing → gateway startup → MQTT validation → browser verification)
- Diagnostic success indicators and recovery playbooks for common connection failures
- Boundary between verified offline behavior and unverified live behavior

## Quick Start

### 1. Gateway (Windows Laptop)

```powershell
cd gateway
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt

# Smoke-test PLC connection (read-only)
python test_plc_connection.py

# Start the gateway
python gateway.py
```

See [docs/gateway-setup.md](docs/gateway-setup.md) for full instructions.

### 2. ESP8266 Firmware

1. Copy `firmware/plc_monitor/secrets.h.example` → `secrets.h`
2. Fill in Wi-Fi and MQTT credentials
3. Open `plc_monitor.ino` in Arduino IDE and flash to NodeMCU

See [docs/firmware-setup.md](docs/firmware-setup.md) for full instructions.

### 3. MQTT Broker (EMQX Cloud Serverless)

1. Create a free Serverless deployment at https://www.emqx.com/en/cloud
2. Note the deployment broker hostname (e.g., `xxxxxx.emqxsl.com`)
3. Create two client credential sets in **Authentication**:
   - `plc_monitor_device` — publish to `plcmon/#`
   - `browser_readonly` — subscribe to `plcmon/#` only
4. Use WebSocket port **8084** (WSS) with path `/mqtt` for the browser
5. Use native MQTT TLS port **8883** for the ESP8266

### 4. Frontend & Offline Execution

No physical hardware, live PLC, or MQTT broker is needed to run and inspect the full application:

```powershell
# Serve the static web application locally
python -m http.server 8080 --directory web

# Open in browser:
# Main Application: http://localhost:8080/index.html
# Automated Tests:  http://localhost:8080/test.html
```

To run Python unit tests:
```powershell
python gateway/test_unit.py
```

### 5. Offline Demo Mode & Communication Fault Simulation

1. Navigate to the **Settings** tab and check **Toggle Offline Demonstration Mode**.
2. The system generates deterministic telemetry pulses every 2 seconds, cycling the `X3` input contact and driving `Y20` in synchronization.
3. Test communication fault scenarios via the scenario control pills:
   - **Normal (2s Cycle):** Nominal scan cadence (X3 pulse driving Y20).
   - **Delayed Telemetry (8s):** Demonstrates latency degradation and stale warnings.
   - **Sequence Gap (+3):** Injects skipped packet sequence numbers to verify gap detection.
   - **Stale Data (>15s):** Halts telemetry delivery to verify the `TELEMETRY STALE` banner and tile degradation.
   - **Gateway Fault & Recovery:** Simulates host gateway COM9 timeout with automatic recovery after 6 seconds.
4. **Pause/Resume** and **Reset** controls allow repeatable demonstrations.

### 6. Windows Local Agent REST API (Phase 04A)

The platform provides a strictly read-only local FastAPI service exposing telemetry snapshots, device inventory, and engine diagnostics to local engineering tools, browser frontends, and SCADA overlays over loopback (`127.0.0.1:8000`).

```powershell
# Run the Local Agent in offline simulation mode:
.\gateway\.venv\Scripts\python.exe -m agent.cli --simulate

# Run with custom port and API key authentication:
.\gateway\.venv\Scripts\python.exe -m agent.cli --simulate --port 8000 --api-key your-secret-key

# View interactive OpenAPI / Swagger documentation:
# Open in browser: http://127.0.0.1:8000/docs
```

To run the full unit test suite (77 tests covering Core, Modbus, and Local Agent):
```powershell
.\gateway\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

#### Read-Only REST Endpoints (`/api/v1`)
| Endpoint | Method | Description |
|---|---|---|
| `/api/v1/health` | `GET` | Gateway health, uptime, polling metrics, error count |
| `/api/v1/devices` | `GET` | List of all configured PLC devices and connection status |
| `/api/v1/devices/{device_id}` | `GET` | Detailed metadata and point definitions for a device |
| `/api/v1/devices/{device_id}/telemetry` | `GET` | Latest telemetry snapshot with point values and quality codes |
| `/api/v1/devices/{device_id}/diagnostics` | `GET` | Adapter-level diagnostics, round-trip times, and error stats |
| `/api/v1/agent/info` | `GET` | Host OS, Python version, agent version, security profile |
| `/api/v1/config/status` | `GET` | Active configuration summary with secrets strictly redacted |

> **Security Note:** The Local Agent binds exclusively to `127.0.0.1` by default and rejects non-loopback bindings (`0.0.0.0`) unless `--allow-remote` is explicitly specified. It contains zero write, force, or control endpoints.

## Supervisory Engineering Features (Stages 1.6 – 1.8)

### 1. Dual Visual Themes (Dark & Light)
- **Dark Slate Theme (Default):** Tailored for 24/7 low-glare industrial control room environments.
- **Light Industrial Theme:** High-contrast steel/slate theme designed for bright ambient shop floor and field tablet inspections.
- **Status Accessibility:** Preserves strict distinctions between `ON`, `OFF`, `UNKNOWN`, `STALE`, and `FAULT` with text badges and borders—never relying on color alone.
- **Theme Persistence:** Stores visual preference in `localStorage` under `qplc_theme` without ever saving credentials or tokens. Early anti-flash bootstrap prevents layout flickers on page load.

### 2. Industrial Monitoring Semantics & Data Integrity (Stage 1.8 Quality Gate)
- **Strict Bit Normalization:** Supported ON values (`1`, `true`, `"1"`, `"true"`, `"on"`) and OFF values (`0`, `false`, `"0"`, `"false"`, `"off"`). All missing, malformed, null, or out-of-spec values resolve to `UNKNOWN` (`null`) and are never displayed as false OFF.
- **State Change Suppression:** Invalid or corrupted data never triggers false ON/OFF transition events.
- **Duplicate Frame Protection:** Duplicate packets sharing identical sequence numbers are discarded to avoid corrupting telemetry statistics.
- **Out-of-Order Frame Rejection:** Delayed packets with older sequence numbers are discarded to prevent live device states from regressing backwards in time.
- **Gateway Reboot Handling:** Clean sequence counter resets (e.g. sequence resetting to 1) re-synchronize the baseline cleanly.
- **Retained / Historic Telemetry Detection:** Packets generated >15 seconds ago (such as MQTT retained messages from previous sessions) are immediately marked as `STALE` rather than presented as live.
- **Missing Dependency Resilience:** If the MQTT client library fails to load from the CDN, the application displays an informative notification without throwing unhandled exceptions.

### 3. Independent Digital I/O Architecture & Read-Back Verification
- **Dual 8-Point Matrix:** Directly monitors 8 physical digital inputs (`X0–X7`) and 8 digital outputs (`Y20–Y27`).
- **Independent Output Actuation:** Full support for commanding individual outputs `Y20` through `Y27` independently (hexadecimal offsets `0x20` to `0x27`).
- **Closed-Loop Read-Back Verification:** Every commanded actuation undergoes 5-stage verification (client validation, gateway safety gate check, PIN authentication, PLC protocol transmission, and post-write telemetry read-back confirmation).
- **Y0 Permanently Excluded:** Point `Y0` is strictly forbidden across all firmware routes, control adapters, and UI selectors.

### 4. Information Architecture & Presentation
- **Overview:** Subsystem telemetry health matrix with independent states (ESP32 Gateway, Q-Series PLC, R-Series PLC), live 8-input and 8-output status overview cards, freshness age metrics, and recent events.
- **PLC I/O Monitor:** Dual-view presentation offering a **Card Grid** (visual LED indicators) and a **Compact Engineering Diagnostics Table** (hex addresses, point types, signal bars, and timestamps).
- **Event Journal:** Real-time event log with multi-category filters (`I/O Changes`, `Comms Faults`, `System`), severity levels (`INFO`, `WARN`, `ERROR`), real-time search, and shift handover CSV export.
- **System Diagnostics:** Telemetry statistics counters (frame counts, heartbeats, gaps, discards, transitions), ESP32 runtime memory diagnostics (heap, PSRAM, flash), and signal path breakdown.
- **Settings:** Browser-local configuration, visual theme picker, and demo mode manager. Operator PINs are never saved in persistent storage.

## Hardware Specifications

| Item | Model / Detail | Network Role |
|---|---|---|
| Station 1 PLC | Mitsubishi Q03UDECPU | `192.168.3.30:5010` (Ethernet, MC 3E Binary) |
| Station 2 PLC | Mitsubishi iQ-R R04 | `192.168.3.40:5010` (Ethernet, MC 3E Binary) |
| Local Router | TP-Link (SSID: `RUBY`) | Local Wi-Fi & Ethernet AP (`192.168.3.1`) |
| Embedded Gateway | ESP32 DevKit (NodeMCU-32S / ESP32-WROOM-32) | Connects to `RUBY` Wi-Fi, hosts HMI, polls PLCs |
| Client Devices | Mobile Phones & Engineering Laptops | Connects to `RUBY` Wi-Fi, accesses HMI at ESP32 IP |

## Security & Fail-Closed Safety Policy

- **Fail-Closed Physical Writes:** In production firmware and software adapters, physical write operations default to `false` (`Q_PLC_WRITES_ENABLED false`, `R_PLC_WRITES_ENABLED false`). Writes remain disabled until physical safety authorization and local commissioning activation.
- **PIN Authorization:** Actuation requires an explicit operator PIN. Empty, placeholder, or default PINs fail closed.
- **Strict Network Isolation:** All communication occurs over the isolated local industrial network provided by `RUBY`. Zero external cloud brokers or WAN dependencies.
- **Credential Hygiene:** Private network credentials and operator PINs reside in `secrets.h` (excluded via `.gitignore`). Templated defaults in `secrets.example.h` are safe placeholders.

## Superseded Documentation Notice

Earlier revisions of project documentation described a ladder rung relationship (`LD X003 -> OUT Y020`). As verified on the physical test bench, the prototype operates with **independent 8-channel inputs (`X0–X7`) and outputs (`Y20–Y27`)**. The ladder coupling assumption is formally superseded.

## License

MIT — See [LICENSE](LICENSE) file.
