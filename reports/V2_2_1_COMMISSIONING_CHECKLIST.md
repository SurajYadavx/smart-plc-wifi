# MITSUBISHI Q/R SERIES DUAL-PLC LOCAL SUPERVISORY SYSTEM
## Field Commissioning and Verification Checklist — Project V2.2.1

**Document ID:** `SOP-Q03-R04-COMM-V2.2.1`  
**Revision:** 2.2.1  
**Target Controllers:** Mitsubishi Q03UDECPU (Station 1), Mitsubishi R04ENCPU (Station 2)  
**Gateway Platform:** ESP32-WROOM-32 (Dual Core 240MHz, 4MB Flash, 520KB SRAM)  
**Safety Classification:** Level 3 Fail-Closed Industrial Telemetry & Output Actuation  

---

## 1. PRE-COMMISSIONING SAFETY MANDATE & INVARIANTS

> [!CAUTION]
> **FAIL-CLOSED POLICY MUST BE MAINTAINED**  
> Physical output writes are strictly disabled by default in both firmware source (`Q_PLC_WRITES_ENABLED = false`, `R_PLC_WRITES_ENABLED = false`) and frontend state. Actuation of physical PLC outputs Y20 through Y27 carries risk of machine movement, solenoid actuation, or electrical contact closure. Do NOT enable physical writes until all Section 2 and Section 3 pre-checks are fully signed off.

### Invariant Rules
1. **Device Point Y0 is Strictly Banned:** Device point `Y0` must never be commanded, polled, or configured. Attempting to write `Y0` must fail closed at UI, REST API, and PLC driver layers.
2. **No Default Credentials:** The gateway does not accept blank, placeholder, or default PINs (`1234`, `0000`). A private commissioning PIN must be provisioned in `secrets.h`.
3. **Station 2 Output Lockout:** Station 2 (R04ENCPU) is strictly limited to read-only register telemetry (`D0..D9`). Physical writes to Station 2 are permanently locked.
4. **Independent FreeRTOS Cores:** The background PLC communication worker runs on Core 0. The HTTP REST web server runs on Core 1. Core 1 web requests must never be blocked by PLC network timeouts.

---

## 2. PHYSICAL BENCH INSPECTION & HARDWARE SETUP

| Step | Item | Description / Verification Requirement | Verified (Y/N) | Initial / Date |
| :---: | :--- | :--- | :---: | :---: |
| **2.1** | **24V DC Power Supplies** | Verify primary industrial 24V DC power rail is stable within 24.0V ± 0.5V. Check ripple < 50mV. | [ ] | ______ / ______ |
| **2.2** | **Ground & Bonding** | Verify common star-ground reference between PLC rack power supply, ESP32 gateway GND, and cabinet chassis. Check resistance to earth < 0.1 Ω. | [ ] | ______ / ______ |
| **2.3** | **ESP32 5V Supply** | Verify ESP32 5V rail is supplied by an isolated 5.0V / 2.0A switching supply. Measure 5V pin: 4.95V–5.15V. Measure 3.3V LDO output: 3.28V–3.32V. | [ ] | ______ / ______ |
| **2.4** | **I2C Bus & LCD 2004** | If LCD 2004 is installed: verify PCF8574 backpack is connected to GPIO21 (SDA) and GPIO22 (SCL). Confirm 4.7kΩ pull-up resistors to 3.3V or 5V level-shifter. If absent, confirm headless mode is expected. | [ ] | ______ / ______ |
| **2.5** | **Ethernet / Network Switch** | Verify industrial 10/100 Mbps unmanaged Ethernet switch is powered. Connect Station 1 Q03UDECPU, Station 2 R04ENCPU, and local engineering PC / Access Point. | [ ] | ______ / ______ |
| **2.6** | **Q03UDECPU Wiring** | Verify Q-Series digital output module (e.g., QY10 / QY40P) terminal strip for points Y20–Y27. Ensure test load indicators (LEDs or 24V indicator lamps) are connected with series resistors. | [ ] | ______ / ______ |
| **2.7** | **Q03UDECPU Inputs** | Verify Q-Series digital input module (e.g., QX40) terminal strip for points X0–X7. Verify dry contact switches or 24V pushbuttons are connected. | [ ] | ______ / ______ |

---

## 3. NETWORK ARCHITECTURE & ADDRESSING VERIFICATION

### 3.1 Network Topology Plan
- **Subnet:** `192.168.3.0/24`
- **Subnet Mask:** `255.255.255.0`
- **Gateway / Switch:** `192.168.3.1`
- **ESP32 Gateway:** `192.168.3.238` (Port 80)
- **Station 1 (Q03UDECPU):** `192.168.3.30` (Port 5010, MC Protocol 3E Binary TCP)
- **Station 2 (R04ENCPU):** `192.168.3.20` (Port 5010, MC Protocol 3E Binary TCP)

### 3.2 Network Reachability Verification Steps

```bash
# 1. Ping test from Commissioning Laptop
ping 192.168.3.238   # ESP32 Local Gateway
ping 192.168.3.30    # Q03UDECPU PLC
ping 192.168.3.20    # R04ENCPU PLC

# 2. Verify PLC MC Protocol Port Accessibility (PowerShell)
Test-NetConnection -ComputerName 192.168.3.30 -Port 5010
Test-NetConnection -ComputerName 192.168.3.20 -Port 5010

# 3. Verify ESP32 Gateway HTTP REST Service
curl http://192.168.3.238/api/v1/health
```

| Step | Target Device | Expected Response | Verified (Y/N) | Latency (ms) |
| :---: | :--- | :--- | :---: | :---: |
| **3.1** | ESP32 Gateway (`192.168.3.238`) | 4/4 ICMP Echo replies received, 0% loss | [ ] | ______ ms |
| **3.2** | Q03UDECPU (`192.168.3.30:5010`) | TCP port 5010 open, `TcpTestSucceeded: True` | [ ] | ______ ms |
| **3.3** | R04ENCPU (`192.168.3.20:5010`) | TCP port 5010 open, `TcpTestSucceeded: True` | [ ] | ______ ms |
| **3.4** | ESP32 REST Health API | `HTTP 200 OK`, JSON `{"status":"HEALTHY","firmware":"2.2.1"}` | [ ] | ______ ms |

---

## 4. GATEWAY BOOT & HEADLESS VS LCD VERIFICATION

| Step | Inspection Item | Verification Procedure | Pass Criteria | Status |
| :---: | :--- | :--- | :--- | :---: |
| **4.1** | **Serial Console Output** | Attach USB to ESP32 at 115200 baud. Monitor boot sequence. | No panic, FreeRTOS worker started on Core 0, web server on Core 1. | [ ] |
| **4.2** | **LCD Detection Check** | Query `GET /api/v1/system`. Check `lcd.state`. | If physical LCD present: `OK` (`0x27` or `0x3F`). If absent: `NOT_DETECTED`. Headless execution continues without delay. | [ ] |
| **4.3** | **LittleFS File Mount** | Check serial log or inspect `/api/v1/system`. | LittleFS mounted successfully. `index.html` (80KB), `style.css` (76KB), `app.js` (136KB) accessible. | [ ] |
| **4.4** | **FreeRTOS Worker Health** | Inspect `GET /api/v1/system` worker block. | `core: 0`, `status: RUNNING`, `min_free_stack_bytes > 2048`. | [ ] |

---

## 5. DEFAULT FAIL-CLOSED SAFETY VALIDATION

Before flashing any production configuration, execute the following safety verification checks to prove that the firmware and frontend are completely fail-closed:

| Step | Security Check | Execution Procedure | Expected Outcome | Pass/Fail |
| :---: | :--- | :--- | :--- | :---: |
| **5.1** | **Default Q-Series Writes** | Query `GET /api/v1/system` | `q_writes_enabled` must be `false`. | [ ] PASS |
| **5.2** | **Default R-Series Writes** | Query `GET /api/v1/system` | `r_writes_enabled` must be `false`. | [ ] PASS |
| **5.3** | **Unauthenticated POST Write** | Submit `POST /api/v1/plc/actuate` with payload `{"station":1,"point":"Y20","state":1}` and no `X-Operator-PIN` header. | HTTP 401 Unauthorized (`MISSING_PIN`). Zero PLC write frames sent. | [ ] PASS |
| **5.4** | **Invalid PIN POST Write** | Submit `POST /api/v1/plc/actuate` with `X-Operator-PIN: 1234` (or wrong PIN). | HTTP 401 Unauthorized (`INVALID_PIN`). Zero PLC write frames sent. | [ ] PASS |
| **5.5** | **Pre-flight Write Disabled** | Submit `POST /api/v1/plc/actuate` with valid PIN while `q_writes_enabled: false`. | HTTP 403 Forbidden (`PHYSICAL_WRITES_DISABLED`). Zero PLC write frames sent. | [ ] PASS |
| **5.6** | **Banned Point Y0 Rejection** | Attempt POST with `{"station":1,"point":"Y0","state":1}`. | HTTP 400 Bad Request (`INVALID_DEVICE_POINT`). Point rejected before PLC adapter. | [ ] PASS |
| **5.7** | **Frontend UI Status** | Open `http://192.168.3.238/` in browser. Observe Station 1 and Station 2 banners. | Station 1: `MONITORING AVAILABLE — CONTROL LOCKED`. Station 2: `REACHABLE — CONFIGURATION UNVERIFIED`. | [ ] PASS |

---

## 6. STATION 1 (Q03UDECPU) TELEMETRY COMMISSIONING

Verify that all 8 physical inputs (X0–X7) and 8 physical outputs (Y20–Y27) are accurately read from PLC device memory via MC Protocol 3E Binary TCP:

### 6.1 Input Telemetry Verification (X0–X7)
Toggle physical input switches on the PLC input rack and verify instant reflection in the web interface and `/api/v1/telemetry`:

| Input Point | Hardware Action | Physical LED | API Value (`inputs.X*`) | UI Status Grid | Pass/Fail |
| :---: | :--- | :---: | :---: | :---: | :---: |
| **X0** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X1** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X2** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X3** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X4** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X5** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X6** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |
| **X7** | Toggle switch ON -> OFF | ON -> OFF | `1` -> `0` | Active (Green) -> Inactive (Gray) | [ ] |

### 6.2 Output Telemetry Verification (Y20–Y27)
Manually force outputs via GX Works2 or bench test routine to confirm monitoring accuracy:

| Output Point | Device Code | Address (Hex) | Read Value (`outputs.Y*`) | UI Status Chip | Verified |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **Y20** | `0x9D` | `0x000014` | `0` / `1` | `Y20: OFF` / `Y20: ON` | [ ] |
| **Y21** | `0x9D` | `0x000015` | `0` / `1` | `Y21: OFF` / `Y21: ON` | [ ] |
| **Y22** | `0x9D` | `0x000016` | `0` / `1` | `Y22: OFF` / `Y22: ON` | [ ] |
| **Y23** | `0x9D` | `0x000017` | `0` / `1` | `Y23: OFF` / `Y23: ON` | [ ] |
| **Y24** | `0x9D` | `0x000018` | `0` / `1` | `Y24: OFF` / `Y24: ON` | [ ] |
| **Y25** | `0x9D` | `0x000019` | `0` / `1` | `Y25: OFF` / `Y25: ON` | [ ] |
| **Y26** | `0x9D` | `0x00001A` | `0` / `1` | `Y26: OFF` / `Y26: ON` | [ ] |
| **Y27** | `0x9D` | `0x00001B` | `0` / `1` | `Y27: OFF` / `Y27: ON` | [ ] |

---

## 7. PHYSICAL WRITE ENABLING PROCEDURE (STATION 1)

Execute this step ONLY after Sections 2 through 6 are fully satisfied and signed off by the Lead Automation Engineer.

### Step 7.1: Physical Area Clearance & E-Stop Verification
- [ ] Ensure all machinery connected to output module is mechanically isolated or locked out.
- [ ] Verify hardwired Emergency Stop (E-Stop) circuit drops 24V supply to output module independently of PLC logic.
- [ ] Safety Sign-Off Signature: _______________________ Date: ______________

### Step 7.2: Firmware Configuration Modification
In file `firmware/esp32_local_gateway/src/config.h`:
```c
// Enable physical writes for Station 1 (Q-Series)
#define Q_PLC_WRITES_ENABLED true

// Station 2 MUST remain false
#define R_PLC_WRITES_ENABLED false
```

### Step 7.3: Provision Commissioning Operator PIN
In file `firmware/esp32_local_gateway/src/secrets.h`:
```c
// Provision non-trivial operator PIN (6 to 12 digits recommended)
#define OPERATOR_PIN "948201"
```

### Step 7.4: Compile & Flash Gateway Firmware
```bash
# Build firmware binary
platformio run -d firmware/esp32_local_gateway

# Flash via USB / UART
platformio run -d firmware/esp32_local_gateway -t upload

# Verify write permission active via REST API
curl -s http://192.168.3.238/api/v1/system | jq .q_writes_enabled
# Expected output: true
```

---

## 8. EIGHT-CHANNEL INDEPENDENT OUTPUT ACTUATION TEST

With physical writes enabled and the private PIN provisioned, test individual output control through the web UI (`http://192.168.3.238/`):

### 8.1 Sequential Actuation Matrix
For each channel, command ON, verify physical contact closure, command OFF, and verify release. Ensure no other output changes state.

| Test ID | Point | Command | PIN Entered | Physical Relay / LED | Crosstalk Check (Other 7 Outputs) | Readback Indicator | Pass/Fail |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **8.1.1** | Y20 | ON (1) | Valid | Closed / LIT | Y21–Y27 remain OFF | `Y20: 1 (ON)` | [ ] |
| **8.1.2** | Y20 | OFF (0) | Valid | Open / DARK | Y21–Y27 remain OFF | `Y20: 0 (OFF)` | [ ] |
| **8.2.1** | Y21 | ON (1) | Valid | Closed / LIT | Y20, Y22–Y27 remain OFF | `Y21: 1 (ON)` | [ ] |
| **8.2.2** | Y21 | OFF (0) | Valid | Open / DARK | Y20, Y22–Y27 remain OFF | `Y21: 0 (OFF)` | [ ] |
| **8.3.1** | Y22 | ON (1) | Valid | Closed / LIT | Y20–Y21, Y23–Y27 remain OFF | `Y22: 1 (ON)` | [ ] |
| **8.3.2** | Y22 | OFF (0) | Valid | Open / DARK | Y20–Y21, Y23–Y27 remain OFF | `Y22: 0 (OFF)` | [ ] |
| **8.4.1** | Y23 | ON (1) | Valid | Closed / LIT | Y20–Y22, Y24–Y27 remain OFF | `Y23: 1 (ON)` | [ ] |
| **8.4.2** | Y23 | OFF (0) | Valid | Open / DARK | Y20–Y22, Y24–Y27 remain OFF | `Y23: 0 (OFF)` | [ ] |
| **8.5.1** | Y24 | ON (1) | Valid | Closed / LIT | Y20–Y23, Y25–Y27 remain OFF | `Y24: 1 (ON)` | [ ] |
| **8.5.2** | Y24 | OFF (0) | Valid | Open / DARK | Y20–Y23, Y25–Y27 remain OFF | `Y24: 0 (OFF)` | [ ] |
| **8.6.1** | Y25 | ON (1) | Valid | Closed / LIT | Y20–Y24, Y26–Y27 remain OFF | `Y25: 1 (ON)` | [ ] |
| **8.6.2** | Y25 | OFF (0) | Valid | Open / DARK | Y20–Y24, Y26–Y27 remain OFF | `Y25: 0 (OFF)` | [ ] |
| **8.7.1** | Y26 | ON (1) | Valid | Closed / LIT | Y20–Y25, Y27 remain OFF | `Y26: 1 (ON)` | [ ] |
| **8.7.2** | Y26 | OFF (0) | Valid | Open / DARK | Y20–Y25, Y27 remain OFF | `Y26: 0 (OFF)` | [ ] |
| **8.8.1** | Y27 | ON (1) | Valid | Closed / LIT | Y20–Y26 remain OFF | `Y27: 1 (ON)` | [ ] |
| **8.8.2** | Y27 | OFF (0) | Valid | Open / DARK | Y20–Y26 remain OFF | `Y27: 0 (OFF)` | [ ] |

### 8.2 Banned Point Y0 Re-Verification
- [ ] Inspect UI dropdown: confirm point `Y0` does NOT appear anywhere in the station point selector.
- [ ] Attempt programmatic write via `curl` to `Y0`:
  ```bash
  curl -X POST http://192.168.3.238/api/v1/plc/actuate \
    -H "Content-Type: application/json" \
    -H "X-Operator-PIN: 948201" \
    -d '{"station":1,"point":"Y0","state":1}'
  ```
- [ ] Verify HTTP 400 Bad Request returned: `{"status":"ERROR","code":"INVALID_DEVICE_POINT"}`.
- [ ] Confirm no physical output relay energizes.

---

## 9. STATION 2 (R04ENCPU) PROVISIONAL MONITORING CHECK

| Step | Item | Procedure | Expected Result | Pass/Fail |
| :---: | :--- | :--- | :--- | :---: |
| **9.1** | **R-Series Polling** | Monitor Core 0 poll cycle for Station 2 (`192.168.3.20:5010`). | Successful read of registers D0–D9. Poll time < 50ms. | [ ] |
| **9.2** | **Telemetry Display** | Select Station 2 in Web UI. | Registers D0–D9 displayed accurately with raw and engineering units. | [ ] |
| **9.3** | **Unverified Status** | Inspect Station 2 header badge. | Displays: `REACHABLE — CONFIGURATION UNVERIFIED`. | [ ] |
| **9.4** | **Output Lockout** | Verify Output Control panel when Station 2 is selected. | Station 2 control points locked. Actuation button disabled. Dropdown shows lock notice. | [ ] |

---

## 10. FAULT RECOVERY & INCIDENT PROCEDURES

| Fault Scenario | Diagnostic Indicator | Automated Response | Operator Action |
| :--- | :--- | :--- | :--- |
| **PLC Disconnected / Powered Off** | Web UI shows Station `OFFLINE`. Gateway log: `TCP connect timeout (1000ms)`. | Core 0 poll worker marks station down, sets error flag. Web server continues serving UI smoothly (<2ms response). | Check PLC power, Ethernet switch port, and IP configuration. Restoring connection clears fault automatically. |
| **Invalid PIN Rate-Limiting Lockout** | HTTP 429 Too Many Requests (`AUTH_RATE_LIMITED`). | Gateway locks actuation API for 30 seconds after 5 failed PIN attempts. | Wait 30 seconds. Verify PIN before re-submitting. |
| **FreeRTOS Worker Watchdog Reset** | ESP32 reboots, serial reports `WDT trigger on Core 0`. | ESP32 hardware watchdog reboots system in < 500ms. Defaults to fail-closed. | Verify network switch latency and ensure PLC is responding within 1000ms timeout. |
| **Emergency Cabinet Shutdown** | Physical E-Stop button pressed. | 24V supply to output relays immediately drops. Relays open instantly. | Investigate cause of emergency. Clear safety zone before resetting E-Stop. |

---

## 11. COMMISSIONING SIGN-OFF & HANDOVER APPROVAL

| Role | Name | Signature | Date |
| :--- | :--- | :--- | :--- |
| **Lead Automation Engineer** | ___________________________ | ___________________________ | ____________ |
| **Electrical Safety Inspector** | ___________________________ | ___________________________ | ____________ |
| **Plant Operations Manager** | ___________________________ | ___________________________ | ____________ |
| **ESP32 Firmware Lead** | ___________________________ | ___________________________ | ____________ |

> [!NOTE]
> Keep a signed physical copy of this document inside the control cabinet enclosure `ENC-PLC-01`. Keep an electronic copy alongside the project handover archive `PROJECT_V2_2_1_HANDOVER.zip`.
