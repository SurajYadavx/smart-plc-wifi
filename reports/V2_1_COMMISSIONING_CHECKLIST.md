# Project V2.1 Laboratory Commissioning Checklist & Safety Protocol
## Q/R Series Dual-PLC Local Network Supervisory and Control System
**Document ID:** CHK-V2.1-COMM-001  
**Classification:** Laboratory Commissioning & Physical Verification Protocol  
**Author:** Industrial Automation & Commissioning Team  
**Date:** November 17, 2026  
**Firmware Version:** `2.1.0`  

---

## 1. Network Topology & Hardware Specification

The local supervisory network operates exclusively on an isolated private subnet hosted by a dedicated TP-Link router named **RUBY**. Zero internet connectivity, cloud infrastructure, or external DNS servers are permitted on this industrial LAN.

| Device Node | Physical Interface | IP Address | Subnet Mask | Gateway | Port / Protocol | Function |
|---|---|---|---|---|---|---|
| **TP-Link Router (RUBY)** | Router LAN / Wi-Fi | `192.168.3.1` | `255.255.255.0` | — | DHCP Server / WPA2-PSK | Central Network Infrastructure |
| **Mitsubishi Q-Series (Q03UDECPU)** | Built-in Ethernet (Port 2) | `192.168.3.30` | `255.255.255.0` | `192.168.3.1` | `5010` / MC Protocol 3E Binary | Station 1 Primary Controller |
| **Mitsubishi iQ-R Series (R04CPU)** | Built-in Ethernet (Port 3) | `192.168.3.20` | `255.255.255.0` | `192.168.3.1` | `5010` / SLMP MC 3E Binary | Station 2 Primary Controller |
| **ESP32 DevKit Gateway** | Wi-Fi Station (2.4 GHz) | `192.168.3.50` | `255.255.255.0` | `192.168.3.1` | `80` (HTTP / Web App / REST) | Central Embedded Supervisory Node |
| **16x2 I2C LCD Display** | I2C Bus (GPIO 21, 22) | Address `0x27` | — | — | 100 kHz I2C | Local Real-Time Diagnostic Screen |
| **Mobile Client / Operator Phone** | Wi-Fi Client (`RUBY`) | `192.168.3.100+` (DHCP) | `255.255.255.0` | `192.168.3.1` | Browser (`http://192.168.3.50`) | Field HMI Supervisory Console |
| **Engineering Laptop** | Wi-Fi Client or Port 4 | `192.168.3.10` (Static/DHCP) | `255.255.255.0` | `192.168.3.1` | GX Works2/3, PlatformIO | Maintenance & Commissioning Host |

---

## 2. Pre-Commissioning Safety Prerequisites

- [ ] **Physical E-Stop Available:** Emergency stop pushbuttons are physically wired into PLC safety circuits.
- [ ] **No Hazardous Loads Connected:** Confirm that output terminals `Y20..Y27` on both PLCs are wired exclusively to indicator lamps, relays, or test benches. No motors, heaters, valves, or actuators may be active during commissioning.
- [ ] **Air-Gapped Isolation:** Confirm TP-Link router RUBY has no WAN Ethernet cable connected. WAN LED must be OFF.
- [ ] **Ladder Program Integrity:** Confirm that existing ladder logic in Q03UDECPU contains:
  ```ladder
  LD  X003
  OUT Y020
  ```
  **Do NOT modify or delete this ladder rung.** The supervisory system is designed to monitor this rung.
- [ ] **Banned Point Rule:** Confirm that physical output `Y0` has **NO WIRES CONNECTED** and is strictly prohibited in software.

---

## 3. Stage-by-Stage Commissioning Procedure

### Stage 1: Network & Router Infrastructure
1. [ ] Power on TP-Link router RUBY.
2. [ ] Connect engineering laptop to Wi-Fi SSID `RUBY` (Password: configured on router label).
3. [ ] Verify laptop IP assignment in range `192.168.3.100–199`.
4. [ ] Ping router gateway:
   ```cmd
   ping 192.168.3.1 -n 4
   ```
   *Expected Result:* 4 replies, < 2 ms.

### Stage 2: Mitsubishi Q-Series PLC Setup (Q03UDECPU)
1. [ ] Connect Q03 built-in Ethernet port to RUBY LAN Port 2 using a CAT5e/CAT6 cable.
2. [ ] Open **GX Works2** on the engineering laptop.
3. [ ] In Project View: Navigate to **PLC Parameter** -> **Built-in Ethernet Port Setting**.
4. [ ] Configure IP Address: `192.168.3.30`, Subnet Mask: `255.255.255.0`, Default Gateway: `192.168.3.1`.
5. [ ] Click **Open Setting** (Ethernet Port Open Settings table):
   - **Protocol:** `TCP`
   - **Open System:** `MC Protocol`
   - **Host Station Port No.:** `5010` (DEC)
6. [ ] Write parameters to PLC CPU and perform a power cycle of the PLC rack.
7. [ ] Ping Q03 from laptop:
   ```cmd
   ping 192.168.3.30 -n 4
   ```
   *Expected Result:* 4 replies, < 1 ms.

### Stage 3: Mitsubishi iQ-R Series PLC Setup (R04CPU / R04ENCPU)
1. [ ] Connect R04 built-in Ethernet port to RUBY LAN Port 3 using a CAT5e/CAT6 cable.
2. [ ] Open **GX Works3** on the engineering laptop.
3. [ ] Navigate to **Parameter** -> **Module Parameter (CPU)** -> **Application Settings** -> **Own Node Settings**.
4. [ ] Configure IP Address: `192.168.3.20`, Subnet Mask: `255.255.255.0`.
5. [ ] Navigate to **External Device Configuration** -> Add **SLMP Connection Module**:
   - **Communication Protocol:** `TCP`
   - **Port No.:** `5010` (DEC)
6. [ ] Write parameters to R04 CPU and perform a power cycle.
7. [ ] Ping R04 from laptop:
   ```cmd
   ping 192.168.3.20 -n 4
   ```
   *Expected Result:* 4 replies, < 1 ms.

### Stage 4: ESP32 Gateway Flashing & Boot Verification
1. [ ] Connect ESP32 DevKit to laptop via micro-USB cable.
2. [ ] Check serial COM port in Device Manager (e.g. `COM3` or `COM5`).
3. [ ] Configure `firmware/esp32_local_gateway/src/secrets.h` with local Wi-Fi credentials:
   ```cpp
   #define SECRET_WIFI_SSID "RUBY"
   #define SECRET_WIFI_PASS "<your-router-password>"
   #define SECRET_OPERATOR_PIN ""  // Leave empty for fail-closed initial commissioning
   ```
4. [ ] Build and flash firmware using PlatformIO:
   ```cmd
   .venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t upload
   .venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t uploadfs
   ```
5. [ ] Open Serial Monitor at 115200 baud.
6. [ ] Verify serial boot log:
   - LittleFS mounted successfully.
   - Wi-Fi connected to `RUBY`, assigned IP: `192.168.3.50`.
   - LCD initialized (or non-blocking bypass logged if unattached).
   - Web server listening on port 80.
   - Q-Series poller connected to `192.168.3.30:5010`.

### Stage 5: Passive Read-Only Telemetry Verification
1. [ ] From mobile phone or laptop connected to `RUBY`, open browser to `http://192.168.3.50`.
2. [ ] Verify HMI loads completely without requiring internet access.
3. [ ] Verify Station 1 (Q03) reports `ONLINE` (green dot).
4. [ ] Physically toggle input contact `X3` on the Q03 input module:
   - Observe HMI: Input `X3` contact tile toggles to `ON` (energized).
   - Observe HMI: Output coil `Y20` tile toggles to `ON` (driven by CPU ladder scan).
   - Observe Overview tab schematic: Contact `X3` closes and energizes coil `Y20`.
5. [ ] De-energize `X3`:
   - Observe HMI: Both `X3` and `Y20` return to `OFF`.
6. [ ] Select Station 2 (R04) on selector:
   - Observe telemetry for Station 2 streams independently without disturbing Station 1.

### Stage 6: Fail-Closed Write Verification (Safety Gate Test)
1. [ ] Navigate to **Supervisory Control** tab on HMI (`http://192.168.3.50`).
2. [ ] Select Point `Y20`, State `ON`, and enter any test PIN.
3. [ ] Click **Execute Actuation**:
   - **Verified Expected Result:** Pipeline Step 1 FAILS with:
     ```
     SAFETY LOCK: Physical output writes disabled (fail-closed default)
     ```
   - HTTP response code: `403 Forbidden` (`PHYSICAL_WRITES_DISABLED`).
   - Physical output `Y20` does NOT cycle.

### Stage 7: Formal Write Enablement Protocol (Laboratory Authorization)

> [!WARNING]
> Only perform Stage 7 if all physical safety conditions in Section 2 are satisfied, no moving machinery is attached, and the lead test engineer has signed authorization below.

1. [ ] Open `firmware/esp32_local_gateway/src/config.h`.
2. [ ] Set:
   ```cpp
   #define Q_PLC_WRITES_ENABLED true   // Commissioned for Q03 physical output testing
   #define R_PLC_WRITES_ENABLED false  // R-Series remains locked
   ```
3. [ ] Open `firmware/esp32_local_gateway/src/secrets.h` and configure a private operator PIN:
   ```cpp
   #define SECRET_OPERATOR_PIN "LabSafety2026!"
   ```
4. [ ] Re-compile and re-flash firmware:
   ```cmd
   .venv\Scripts\python.exe -m platformio run -d firmware/esp32_local_gateway -t upload
   ```
5. [ ] Test Output Actuation from HMI:
   - Enter PIN: `LabSafety2026!`
   - Select `Y20`, State: `ON`.
   - Click **Execute Actuation**.
   - **Active Ladder Scan Observation:**
     - The write packet sets Y20 memory bit to 1.
     - If input contact `X3` is OPEN (0), the PLC scan cycle (`LD X3 -> OUT Y20`) will reset Y20 to 0 within 1–5 ms.
     - The ESP32 read-back verification will read `0`, correctly reporting:
       ```
       Read-back verification mismatch: Commanded: 1, Read: 0
       Active ladder scan LD X3 -> OUT Y20 may be overriding coil state.
       ```
     - This empirically verifies that ladder program authority takes physical precedence over external supervisory writes.

---

## 4. Commissioning Sign-Off & Approvals

| Role | Engineer Name | Signature | Date | Status |
|---|---|---|---|---|
| **Lead Automation Specialist** | ____________________ | ____________________ | _________ | [ ] APPROVED |
| **Embedded Firmware Engineer** | ____________________ | ____________________ | _________ | [ ] APPROVED |
| **Electrical Safety Officer** | ____________________ | ____________________ | _________ | [ ] APPROVED |
