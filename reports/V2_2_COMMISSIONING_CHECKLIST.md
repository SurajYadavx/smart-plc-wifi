# PROJECT V2.2 — PHYSICAL LABORATORY COMMISSIONING CHECKLIST & RUNBOOK
## MITSUBISHI Q/R SERIES DUAL-PLC LOCAL SUPERVISORY SYSTEM

---

## 1. Safety Prerequisites & Cardinal Rules

> [!CAUTION]
> **READ CAREFULLY BEFORE APPLYING POWER TO PHYSICAL AUTOMATION RIGS:**
> 1. **Fail-Closed Write Rule:** Default firmware builds have physical write commands **disabled** (`Q_PLC_WRITES_ENABLED false`, `R_PLC_WRITES_ENABLED false`). Do not enable physical writes until all passive read-only monitoring tests have succeeded.
> 2. **Emergency Stop (E-Stop):** Ensure an external, hardwired emergency stop contact or power disconnect switch is within immediate reach of the test bench operator.
> 3. **Permanent Y0 Ban:** Device `Y0` is strictly excluded from all routes, allowlists, and memory mappings. Never attempt to map `Y0` to software controls.
> 4. **No Ladder Dependencies:** The active bench operates on **independent digital I/O** (inputs `X0–X7` and outputs `Y20–Y27`). Do not attempt to force `X3` to actuate `Y20`.

---

## 2. Hardware Topology & Network Map

```text
                        ┌─────────────────────────────────┐
                        │   TP-Link Wi-Fi Router "RUBY"   │
                        │   IP: 192.168.3.1 / 255.255.255.0│
                        └───────┬──────────────┬──────────┘
                                │              │
                     (Ethernet) │              │ (Wi-Fi 2.4 GHz)
            ┌───────────────────┴──┐           │
            │                      │           ▼
            ▼                      ▼   ┌──────────────────────────────┐
┌─────────────────────┐  ┌─────────────────────┐  │ ESP32 Local Gateway DevKit   │
│ Station 1: Q-Series │  │ Station 2: iQ-R     │  │ IP: 192.168.3.150 (Static/DHCP)
│ Model: Q03UDECPU    │  │ Model: R04 / R04EN  │  │ Firmware V2.2.0              │
│ IP: 192.168.3.30    │  │ IP: 192.168.3.40    │  │ Web HMI: Port 80             │
│ Port: 5010 (TCP)    │  │ Port: 5010 (TCP)    │  └──────────────┬───────────────┘
│ Protocol: MC 3E Bin │  │ Protocol: MC 3E Bin │                 │
│ Subcommand: 0x0001  │  │ Subcommand: 0x0003  │                 ▼
│ Status: Active I/O  │  │ Status: Read-Only   │       ┌──────────────────┐
└─────────────────────┘  └─────────────────────┘       │ Mobile Phone HMI │
                                                       │ Browser Client   │
                                                       └──────────────────┘
```

---

## 3. Pre-Commissioning Checklist

### 3.1 Network Verification
- [ ] Connect TP-Link router **RUBY** to power.
- [ ] Verify 2.4 GHz Wi-Fi SSID is broadcasting as `RUBY`.
- [ ] Connect laptop to `RUBY` via Ethernet or Wi-Fi. Verify gateway IP is `192.168.3.1`.
- [ ] Verify DHCP pool or static IP leases:
  - Q03UDECPU: `192.168.3.30`
  - iQ-R R04: `192.168.3.40`
  - ESP32 Gateway: `192.168.3.150` (or dynamically assigned)

### 3.2 Mitsubishi PLC Parameter Validation (GX Works2 / GX Works3)
- [ ] **Station 1 (Q03UDECPU):**
  - Open GX Works2 project.
  - Navigate to **PLC Parameters** &rarr; **Built-in Ethernet Port Settings**.
  - IP Address: `192.168.3.30`, Subnet Mask: `255.255.255.0`.
  - Open Settings: Protocol: `TCP`, Open System: `MC Protocol`, Port No.: `5010` (Decimal).
  - Verify CPU switch is set to `RUN`.
- [ ] **Station 2 (iQ-R R04):**
  - Open GX Works3 project.
  - Navigate to **Module Parameter** &rarr; **Basic Settings** &rarr; **Own Node Settings**.
  - IP Address: `192.168.3.40`, Subnet Mask: `255.255.255.0`.
  - External Device Configuration: Protocol: `SLMP Connection` / `MC Protocol`, Port: `5010` (Decimal).

### 3.3 ESP32 Firmware Preparation
- [ ] In `firmware/esp32_local_gateway/src/secrets.h` (create from `secrets.example.h` if missing):
  - Configure `WIFI_SSID` ("RUBY") and `WIFI_PASSWORD`.
  - Configure deliberate operator authorization PIN (e.g. `#define OPERATOR_PIN "2026"`).
  - Leave `Q_PLC_WRITES_ENABLED false` for initial passive verification.
- [ ] Build and flash firmware:
  ```powershell
  .venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t upload
  ```
- [ ] Build and flash LittleFS filesystem:
  ```powershell
  .venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t uploadfs
  ```

---

## 4. Phase 1 — Gateway Boot & Hardware Introspection Verification

1. Open serial terminal at 115200 baud on ESP32 COM port:
   ```powershell
   .venv\Scripts\python.exe -m platformio device monitor -d firmware/esp32_local_gateway -b 115200
   ```
2. Verify startup logs:
   - [ ] `[SYSTEM] Mitsubishi Q/R Series Dual-PLC Local Supervisory Gateway V2.2.0 Booting...`
   - [ ] `[HARDWARE] Chip Model: ..., Flash Size: 4194304 bytes`
   - [ ] `[MEMORY] Free Heap: >250000 bytes`
   - [ ] `[WIFI] Connected to RUBY, IP: 192.168.3.xxx`
   - [ ] `[LITTLEFS] Mounted successfully`
   - [ ] `[HTTP] Web server started on port 80`
   - [ ] `[PLC] Station 1 (Q03UDECPU) connected`
   - [ ] `[PLC] Station 2 (R04) connected (or reported offline if not connected)`

---

## 5. Phase 2 — Passive Monitoring Verification (Zero Writes)

1. Connect smartphone to Wi-Fi `RUBY`.
2. Open mobile browser and navigate to `http://192.168.3.xxx/` (ESP32 IP).
3. Verify HMI Dashboard loads cleanly:
   - [ ] Subsystem status pills show: `GATEWAY: Online`, `Q-PLC: Connected`.
   - [ ] Overview tab displays **INDEPENDENT DIGITAL I/O ARCHITECTURE** card.
   - [ ] Mode badge indicates: `FAIL-CLOSED (WRITES DISABLED)`.
4. **Input Points Validation (`X0` through `X7`):**
   - [ ] Toggle physical input `X0` on test rig: verify tile `X0` turns Green (ON). Release: returns to OFF.
   - [ ] Toggle physical input `X1` on test rig: verify tile `X1` turns Green (ON).
   - [ ] Repeat for `X2`, `X3`, `X4`, `X5`, `X6`, `X7`.
   - [ ] Confirm input toggling does NOT affect any output state `Y20`–`Y27`.
5. **Output Points Passive Read-Back (`Y20` through `Y27`):**
   - [ ] Manually toggle physical output relays (via PLC manual toggle or GX Works2 monitor):
   - [ ] Verify corresponding tiles `Y20` through `Y27` in the PLC I/O tab and the live read-back chips accurately reflect physical contact states.

---

## 6. Phase 3 — Controlled Output Actuation Verification

> [!IMPORTANT]
> **PHYSICAL SAFETY APPROVAL REQUIRED BEFORE PROCEEDING:**
> Perform this phase only after completing Phase 2 and verifying all machine areas around actuators are clear.

1. **Enable Q-Series Writes in Firmware:**
   - In `firmware/esp32_local_gateway/src/config.h` (or via private override in `secrets.h`):
     ```cpp
     #define Q_PLC_WRITES_ENABLED true
     ```
   - Recompile and upload firmware.
2. **Reload Web Interface:**
   - Verify Mode tag transitions to `LOCAL SUPERVISORY (ACTUATION AUTHORIZED)`.
   - Verify dropdown `#ctrl-select-point` populates all 8 points `Y20` through `Y27`.
3. **Negative Authentication Test (Invalid PIN):**
   - Select point `Y20`.
   - Enter invalid PIN `9999`. Click **Set Y20 ON**.
   - [ ] Verify Stage 1 passes.
   - [ ] Verify Stage 2 fails with `Authorization Failed (401)`.
   - [ ] Confirm no physical relay clicks on PLC rack.
4. **Independent Channel Actuation Tests (Valid PIN):**
   - Enter configured operator PIN.
   - **Test Output Y20:**
     - [ ] Command `Y20 ON`: Relay `Y20` clicks ON. Status chip `#chip-out-Y20` shows `LIVE READ-BACK: ON`.
     - [ ] Command `Y20 OFF`: Relay `Y20` clicks OFF. Status chip `#chip-out-Y20` shows `LIVE READ-BACK: OFF`.
   - **Test Output Y21:**
     - [ ] Command `Y21 ON`: Relay `Y21` clicks ON. Status chip `#chip-out-Y21` shows `LIVE READ-BACK: ON`. Outputs `Y20` and `Y22`–`Y27` remain untouched!
     - [ ] Command `Y21 OFF`: Relay `Y21` clicks OFF.
   - **Test Outputs Y22 through Y27:**
     - [ ] Repeat independent ON/OFF command cycle for `Y22`, `Y23`, `Y24`, `Y25`, `Y26`, and `Y27`.
     - [ ] Verify every actuation completes the 5-stage pipeline with 5 green checkmarks.
     - [ ] Confirm each point operates independently without affecting adjacent outputs.
5. **Station 2 Lockout Invariant:**
   - Switch to Station 2 (`melsec_r04`).
   - Navigate to Supervisory Control tab.
   - [ ] Verify banner displays: `Outputs Locked (Provisional)`.
   - [ ] Verify actuation buttons are disabled.

---

## 7. Commissioning Sign-Off

| Milestone Check | Requirement | Result | Engineer Initials |
|---|---|---|---|
| **Network & Comms** | RUBY Wi-Fi connected, MC 3E binary frames streaming | [ ] PASS | ________ |
| **Fail-Closed Gate** | Writes disabled by default, empty PIN rejected | [ ] PASS | ________ |
| **Input Bank (X0–X7)** | 8 inputs independently monitored without ladder coupling | [ ] PASS | ________ |
| **Output Bank (Y20–Y27)** | 8 outputs independently switched and read back | [ ] PASS | ________ |
| **Y0 Exclusion** | Y0 completely absent from routes and allowlists | [ ] PASS | ________ |
| **Station 2 Lockout** | R-Series outputs locked in read-only mode | [ ] PASS | ________ |

**Final Commissioning Status:** [ ] APPROVED FOR AUTONOMOUS LOCAL SUPERVISION
