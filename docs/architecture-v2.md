# Project V2 Architecture Specification
## Q/R Series Dual-PLC Local Network Supervisory and Control System

---

## 1. System Identity and Executive Summary

**Project Name:** Q/R Series Dual-PLC Local Network Supervisory and Control System (Project V2)  
**System Classification:** Industrial Embedded Supervisory Control and Data Acquisition (SCADA) Gateway & Mobile HMI  
**Target Controllers:** 
- Primary Station 1: Mitsubishi Electric MELSEC-Q Series (`Q03UDECPU`)
- Secondary Station 2: Mitsubishi Electric iQ-R Series (`R04CPU / R04ENCPU`, provisional model classification)
**Network Environment:** Dedicated air-gapped industrial LAN hosted on standalone router (`RUBY`)  
**Central Gateway:** Espressif ESP32 DevKit (Embedded Wi-Fi to Dual-PLC TCP Gateway)

---

## 2. Architectural Paradigm Shift: V1 vs. V2

### 2.1 The Legacy Architecture (V1.6.0)
The historical implementation relied on a multi-hop, cloud-dependent and host-dependent pipeline:
```text
[ Physical Q-PLC ]
       │  (MC Protocol 3E / TCP)
[ Windows Python Gateway (COM9) ]
       │  (USB Serial JSON + Stop-and-Wait ACK)
[ ESP8266 NodeMCU Bridge ]
       │  (Wi-Fi WPA2 + TLS 1.2)
[ EMQX Cloud MQTT Broker ]
       │  (WSS / Port 8084)
[ Web Browser Client (GitHub Pages) ]
```
*V1 Limitations:* Required a running Windows laptop running Python, USB serial bridges, active internet connectivity for EMQX Cloud, and cloud MQTT latency. Output actuation was strictly prohibited in software.

### 2.2 The Project V2 Architecture
Project V2 eliminates all cloud brokers, external internet dependencies, USB serial transports, and permanently tethered laptop gateways. The ESP32 DevKit assumes full ownership of local communication, web hosting, and telemetry caching:

```text
                        ┌─────────────────────────────────────────┐
                        │        OPERATOR CLIENT TIER             │
                        │  Mobile Smartphones & Lab Workstations   │
                        │    (Chrome / Safari / Firefox Mobile)   │
                        └────────────────────┬────────────────────┘
                                             │ HTTP / REST / SSE (Port 80)
                                             │ (Offline Local Web App)
                        ┌────────────────────┴────────────────────┐
                        │      LOCAL NETWORK INFRASTRUCTURE       │
                        │        TP-Link Router ("RUBY")          │
                        │        Subnet: 192.168.3.0/24           │
                        │         Wi-Fi: 2.4 GHz 802.11b/g/n      │
                        │         Ethernet LAN: 100BASE-TX        │
                        └────────────────────┬────────────────────┘
                                             │ Wi-Fi Station (DHCP 192.168.3.x)
                        ┌────────────────────┴────────────────────┐
                        │       CENTRAL EMBEDDED GATEWAY          │
                        │            ESP32 DevKit                 │
                        │  - LittleFS Static Web Server (Port 80) │
                        │  - RESTful JSON API (/api/v1/...)       │
                        │  - Dual-Socket Non-Blocking MC Poller   │
                        │  - Output Safety Allowlist & PIN Auth   │
                        │  - 5-Stage Verification Pipeline Tracker│
                        │  - 16x2 I2C Diagnostic Display (0x27)   │
                        └──────────┬───────────────────┬──────────┘
                                   │                   │
         MC Protocol 3E Binary/TCP │                   │ SLMP / MC Protocol 3E Binary/TCP
         Port 5010 (Subcmd 0x0001) │                   │ Port 5010 (Subcmd 0x0003)
                                   ▼                   ▼
                     ┌──────────────────┐ ┌────────────────────────┐
                     │ STATION 1 (Q-PLC)│ │ STATION 2 (R-PLC)      │
                     │  Q03UDECPU       │ │  R04CPU / R04ENCPU     │
                     │  192.168.3.30    │ │  192.168.3.20          │
                     │  Port: 5010      │ │  Port: 5010            │
                     │  - X0..X7 (In)   │ │  - X0..X7 (In, prov)   │
                     │  - Y20..Y27 (Out)│ │  - Y20..Y27 (Out, prov)│
                     │  - Y20 (Permitted│ │  - Outputs LOCKED      │
                     │    Control Coil) │ │    (Pending Lab Verif) │
                     └──────────────────┘ └────────────────────────┘
```

---

## 3. Subsystem Breakdown

### 3.1 Network Tier: Standalone Router RUBY
- **Subnet:** `192.168.3.0/24` (Subnet mask `255.255.255.0`).
- **Default Gateway / DNS:** `192.168.3.1`.
- **Operating Mode:** Fully isolated local access point (air-gapped; WAN port disconnected).
- **Wi-Fi SSID:** `RUBY` (2.4 GHz band mandatory for ESP32 802.11b/g/n radio).
- **Physical Ethernet:**
  - LAN Port 1: Mitsubishi Q-Series PLC (`192.168.3.30`).
  - LAN Port 2: Mitsubishi iQ-R Series PLC (`192.168.3.20`).
  - LAN Port 3: Engineering Maintenance Workstation (`192.168.3.10`).

### 3.2 Embedded Gateway Tier: ESP32 DevKit
- **Microcontroller:** ESP32-WROOM-32 (Xtensa Dual-Core 32-bit LX6, 240 MHz, 520 KB SRAM, 4 MB Flash).
- **Flash Partitioning:** Default with LittleFS filesystem (`data/` directory hosting static HTML/CSS/JS).
- **Embedded Web Server:** High-throughput `WebServer` serving:
  - Static HMI assets: `/index.html`, `/test.html`, `/assets/css/style.css`, `/assets/js/app.js`.
  - Dynamic API:
    - `GET /api/v1/system`: Gateway uptime, heap, network IP, MAC, LCD status.
    - `GET /api/v1/devices`: Registered device catalog (`melsec_q03`, `melsec_r04`).
    - `GET /api/v1/devices/{device_id}/telemetry`: Normalized telemetry batches.
    - `GET /api/v1/devices/{device_id}/diagnostics`: Socket health, poll cycles, timeouts, error counts.
    - `POST /api/v1/devices/{device_id}/outputs/{point_id}/command`: Output actuation with PIN auth.
- **PLC Poller Engine:** Cooperative, non-blocking polling loops running on 200 ms intervals:
  - Separate `WiFiClient` TCP sockets for Station 1 (`192.168.3.30:5010`) and Station 2 (`192.168.3.20:5010`).
  - Asynchronous state machines with connect, send, wait-response, unpack, and timeout phases.
  - Complete fault isolation: communication failure or disconnect on Station 2 does not impair or stall telemetry from Station 1.
- **Local Diagnostics:** 16x2 I2C LiquidCrystal display (address `0x27`, SDA GPIO 21, SCL GPIO 22):
  - Line 1: Wi-Fi RSSI and IP address.
  - Line 2: Station 1 & 2 link status (`Q:OK R:--`) and last fault code.
  - Auto-detection: runs headless without freezing if display is unpowered or disconnected.

### 3.3 Controller Tier: Dual Mitsubishi Stations

#### Station 1: MELSEC Q-Series (`Q03UDECPU`)
- **IP / Port:** `192.168.3.30:5010` (Static configuration via GX Works2 Built-in Ethernet).
- **Protocol:** MC Protocol 3E Binary format (Command `0x0401` batch read, Command `0x1401` batch write).
- **Subcommand:** `0x0001` (Bit units, 3-byte address, 1-byte device code `0x9C` for X, `0x9D` for Y).
- **Monitored Points:**
  - Inputs: `X0`, `X1`, `X2`, `X3`, `X4`, `X5`, `X6`, `X7`.
  - Outputs: `Y20`, `Y21`, `Y22`, `Y23`, `Y24`, `Y25`, `Y26`, `Y27`.
- **Safety Invariant:** `X3 -> Y20` is an active ladder rung in the physical CPU.
- **Authorized Control Output:** `Y20` only.
- **Banned Point:** `Y0` is strictly rejected and never monitored or commanded.

#### Station 2: MELSEC iQ-R Series (`R04CPU / R04ENCPU`, Provisional)
- **IP / Port:** `192.168.3.20:5010` (Static configuration via GX Works3 Module Parameter).
- **Protocol:** SLMP / MC Protocol 3E Binary format (Command `0x0401` batch read, Command `0x1401` batch write).
- **Subcommand:** `0x0003` (Bit units, 4-byte address, 2-byte device code `0x9C 0x00` for X, `0x9D 0x00` for Y).
- **Provisional Points:** `X0..X7`, `Y20..Y27`.
- **Operational Status:** Strictly Read-Only (Control outputs locked in software until physical CPU model, network card, and ladder interlocks are verified in the laboratory).

### 3.4 Operator Client Tier: Mobile & Desktop Browsers
- **Offline Delivery:** Assets served directly from ESP32 LittleFS flash. Works without internet access.
- **Mobile Responsive Design:** Multi-tier fluid grid layout optimized for smartphone touchscreens.
- **ISA-101 Situational Awareness:** High contrast, low-glare dark palette by default, muted colors for normal states, vibrant amber/red exclusively for alarms.
- **5-Stage Verification Pipeline UI:** Step-by-step visual tracker rendering every stage of an output command.

---

## 4. Supervisory Control Architecture & Safety Interlocks

### 4.1 Safety Principles
1. **Never Write on Boot:** All output registers and coils remain untouched during system initialization.
2. **Strict Output Allowlist:** Only explicitly enumerated point IDs may be commanded (`melsec_q03: ["Y20"]`). All others are rejected immediately.
3. **Permanent Y0 Prohibition:** Point `Y0` was mistakenly included in early conceptual sketches. In the physical machine, `Y0` may be tied to critical emergency circuits. It is hardcoded as a safety violation across all firmware, backend, and frontend validators.
4. **Mandatory Operator PIN:** Every control actuation requires an authorized operator credential (`X-Operator-PIN` header and JSON body token).
5. **Station 2 Lockout:** Station 2 outputs cannot be actuated from the UI or gateway until physical commissioning is complete.
6. **Immediate Read-Back Verification:** Immediately after sending a write command (`0x1401`) and confirming execution end-code `0x0000`, the gateway issues a read command (`0x0401`) to physically verify that the coil energized or de-energized.
7. **Read-back Mismatch Alert:** If the physical read-back contradicts the commanded state (e.g. ladder rung override, contact welded, or power failure), a high-priority alert is displayed.

### 4.2 5-Stage Verification Pipeline
Every actuation request traverses five deterministic gates:
```text
[ Stage 1: Pre-flight Safety Checks ]
       │  • Allowlist check: point == Y20
       │  • Banned check: point != Y0
       │  • Station lock check: station != melsec_r04
       │  • PIN credential check: PIN == "1234"
       ▼
[ Stage 2: Protocol Frame Assembly ]
       │  • Build MC Protocol 3E Binary frame
       │  • Command 0x1401, Subcommand 0x0001
       │  • Device code 0x9D (Y), Address 0x000020
       │  • High-nibble bit data: 0x10 (ON) or 0x00 (OFF)
       ▼
[ Stage 3: Local Network Dispatch ]
       │  • Open TCP socket to 192.168.3.30:5010
       │  • Transmit 22-byte binary frame
       │  • Await TCP ACK / response within 3000 ms
       ▼
[ Stage 4: PLC Execution & Endcode Check ]
       │  • Receive 11-byte response frame
       │  • Verify header (0xD0 0x00)
       │  • Inspect Endcode at bytes 9-10 (Must equal 0x0000)
       ▼
[ Stage 5: Post-Write Read-Back Verification ]
       │  • Issue immediate 0x0401 batch read for Y20
       │  • Verify physical contact state == commanded state
       │  • Surface verified status to operator UI (<50 ms total)
```

---

## 5. Telemetry Semantics & Data Quality

### 5.1 The Valid-Zero Invariant
In industrial automation, digital state `0` (False / OFF) is valid physical information, NOT an error or absence of data.
- **Raw Value `0` / `False`:** Must be packaged with `DataQuality.GOOD` and rendered as `OFF (Open / De-energized)`.
- **Communication Failure / Timeout:** Must be packaged with `DataQuality.COMM_FAILURE` and `raw_value = None`. Rendered as `UNKNOWN / COMM FAILURE` (never rendered as `OFF`, which could mislead an operator into believing an energized circuit is safe).

---

## 6. Preservation of Historical Capabilities

The repository maintains full backward compatibility for previous development phases:
1. **Windows Local Agent (`agent/`):** FastAPI application with background polling, Windows Credential Manager integration, and local telemetry cache.
2. **Modbus TCP Adapter (`adapters/modbus/`):** Industrial Modbus TCP client supporting coils, discrete inputs, input registers, and holding registers.
3. **Simulation Adapter (`core/simulated_adapter.py`, `core/simulated_control_adapter.py`):** Deterministic in-memory simulated controllers for testing without hardware.
4. **Gateway Flow Control (`gateway/`):** Python serial transport with stop-and-wait ACK protocol and ESP8266 firmware retained under `firmware/plc_monitor/`.
