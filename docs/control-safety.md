# Control Safety Architecture & Actuation Verification Specification
## Project V2 — Multi-Gate Safety Interlocks, PIN Security & Read-Back Verification

---

## 1. Safety Philosophy & Scope Transition

The historical version of this application was strictly read-only.
Project V2 introduces **authorized supervisory actuation** while establishing strict industrial safety boundaries to ensure that software operations can never inadvertently trigger hazardous physical conditions.

### Core Safety Tenets:
1. **Never Write on Boot:** Neither the ESP32 firmware nor the Python backend will issue write commands to any PLC during startup, reconnection, or network recovery.
2. **Explicit Output Allowlist:** Only explicitly approved coils and registers may be commanded. The current approved allowlist contains **exactly one point**:
   - `melsec_q03`: `["Y20"]`
   - `melsec_r04`: `[]` (Completely locked)
3. **Permanent Prohibition of Y0:** Actuation of point `Y0` is permanently forbidden. Any command naming `Y0` triggers an immediate security and safety violation.
4. **Mandatory Credential Authentication:** Operators must supply an authorized PIN token before any command reaches the network stack.
5. **Two-Way Closed-Loop Verification:** A command is never marked successful simply because a TCP packet left the gateway. It is only verified when the PLC CPU returns a clean completion code (`0x0000`) AND an immediate read-back confirms the physical contact closed or opened.

---

## 2. Multi-Gate Defense-in-Depth Architecture

Every actuation request must successfully clear four independent architectural boundaries before reaching a physical relay:

```text
[ OPERATOR UI / BROWSER CLIENT ]
  ├── Boundary 1: Station Selector Lockout (Blocks R04)
  ├── Boundary 2: UI Dropdown Allowlist (Exposes Y20 only; Y0 omitted)
  ├── Boundary 3: Pre-Flight Safety Validation (Validates PIN & Y0 check)
         │
         ▼ (HTTP POST /api/v1/devices/{id}/outputs/{point}/command)
[ ESP32 FIRMWARE / BACKEND GATEWAY ]
  ├── Boundary 4: HTTP Header & Body PIN Authentication
  ├── Boundary 5: Device-Level Allowlist Check (config.h / devices.yaml)
  ├── Boundary 6: Strict Y0 Rejection (Hardcoded error return)
  ├── Boundary 7: Station 2 Firmware Lockout (Returns HTTP 403)
         │
         ▼ (MC Protocol 3E Binary TCP Frame / Port 5010)
[ MITSUBISHI PLC CPU ]
  ├── Boundary 8: CPU Memory Protection & Run/Stop Key Switch
  ├── Boundary 9: CPU Cyclic Ladder Scan Logic (Evaluates physical rungs)
```

---

## 3. The 5-Stage Verification Pipeline

The supervisory UI features a real-time 5-stage verification pipeline tracker:

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ 5-STAGE ACTUATION VERIFICATION PIPELINE                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ [1] Pre-Flight Safety Checks                                                │
│     Validates station allowlist, PIN presence, and Y0 prohibition.          │
│                                                                             │
│ [2] Protocol Frame Assembly                                                 │
│     Constructs MC Protocol 3E Binary frame (Cmd 0x1401, Subcmd 0x0001).      │
│                                                                             │
│ [3] Local Network Dispatch                                                  │
│     Transmits 22-byte TCP packet to 192.168.3.30:5010; awaits TCP ACK.      │
│                                                                             │
│ [4] PLC Execution & Endcode Check                                           │
│     Validates 11-byte PLC response header and verifies Endcode == 0x0000.   │
│                                                                             │
│ [5] Post-Write Read-Back Verification                                       │
│     Issues immediate 0x0401 batch read for commanded point (Y20–Y27)        │
│     and verifies physical contact state matches commanded value.            │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Pipeline Failure Outcomes:
- **Failure at Stage 1:** Request aborted before network transmission. Shows "Pre-flight Safety Violation".
- **Failure at Stage 3:** Shows "Network / PLC Timeout". PLC was unreachable or port 5010 was refused.
- **Failure at Stage 4:** Shows "PLC Endcode Error". PLC received the command but rejected it (e.g. write-protected CPU memory).
- **Failure at Stage 5:** Shows **"Read-Back Mismatch Alert"**. The PLC accepted the write, but the contact did not stay in the commanded state (indicating an internal interlock or hardware fault).

---

## 4. Operator Authorization & Fail-Closed PIN Policy

### Fail-Closed Credential Enforcement:
- **No Insecure Default PIN:** In Project V2.2, placeholder PIN `1234` has been removed. Missing, empty, or unconfigured operator PINs fail closed.
- **Configuration Location (ESP32):** `firmware/esp32_local_gateway/src/secrets.h` (`#define OPERATOR_PIN "..."`). Excluded from Git via `.gitignore`.
- **Configuration Location (Backend):** Environment variable `OPERATOR_PIN` or system credential manager.

### Authorization Rules:
1. Blank or whitespace PINs are rejected at Stage 1.
2. Failed PIN submissions return HTTP 401 Unauthorized.
3. Successful actuations log the timestamp, target station, point ID, commanded state, and operator credential verification into the Event Journal.

---

## 5. Secondary Station (R-Series) Lockout Rationale

Station 2 (`melsec_r04` at `192.168.3.20:5010`) is provisionally configured for telemetry monitoring only.
Its control capabilities are locked in both firmware and frontend code because:
1. The physical CPU model (`R04CPU` vs `R04ENCPU`) and attached output modules must be verified on the test bench.
2. The SLMP subcommand (`0x0003`) and 4-byte address encoding must be validated against the active GX Works3 project.
3. The ladder program loaded in the R-Series CPU has not yet been audited for safety interlocks.

Unlocking Station 2 outputs requires updating the allowlist in `config/devices.yaml` and `src/config.h` after physical laboratory commissioning.
