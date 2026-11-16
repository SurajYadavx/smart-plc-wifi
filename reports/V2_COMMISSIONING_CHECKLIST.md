# Physical Laboratory Commissioning Checklist
## Q/R Series Dual-PLC Local Supervisory & Control System (Project V2)

**Test Location:** Industrial Automation Laboratory  
**Target Hardware:** Mitsubishi Q03UDECPU, Mitsubishi R04CPU/R04ENCPU, ESP32 DevKit, TP-Link RUBY  
**Lead Commissioning Engineer:** __________________________  
**Date of Commissioning:** __________________________  

---

## Phase 1: Electrical & Hardware Safety Verification

> [!CAUTION]
> **DO NOT CONNECT THE ESP32 TO 24V POWER UNTIL STEP 1.4 IS SIGNED OFF.**

- [ ] **1.1 Visual Inspection:** Inspect ESP32 DevKit, LM2596 DC-DC step-down buck converter, and 16x2 I2C display backpack for solder bridges, loose terminals, or mechanical defects.
- [ ] **1.2 Isolated 24V Connection:** Wire 24V DC from the PLC panel to the **Input** terminals of the LM2596 buck converter. Keep converter **Output** wires disconnected from the ESP32.
- [ ] **1.3 DMM Voltage Measurement:** Power on the 24V DC power supply. Place calibrated DMM probes on the buck converter output terminals.
  - Measured Output Voltage: `______ V DC`
- [ ] **1.4 Trim Potentiometer Calibration:** Adjust the brass screw on the buck converter until the DMM reads **exactly 5.00V DC (tolerance: 4.95V – 5.05V)**.
  - Calibrated Voltage: `______ V DC` (Sign-off: __________)
- [ ] **1.5 Power-Off & Connection:** Power off the 24V supply. Connect buck converter 5.0V output to ESP32 **VIN** and **GND**. Connect I2C LCD (VCC -> 5V, GND -> GND, SDA -> GPIO 21, SCL -> GPIO 22).

---

## Phase 2: Router Infrastructure Setup (TP-Link RUBY)

- [ ] **2.1 Power & Access:** Power on router RUBY. Log in to web admin (`http://192.168.0.1` or `http://192.168.3.1`).
- [ ] **2.2 LAN Configuration:** Set LAN IP Address to `192.168.3.1`, Subnet Mask to `255.255.255.0`.
- [ ] **2.3 DHCP Pool:** Enable DHCP Server with pool `192.168.3.100` to `192.168.3.200`.
- [ ] **2.4 Wi-Fi Radio Settings:** Enable 2.4 GHz radio. Set SSID to `RUBY`. Set Channel Width to `20 MHz`. Select fixed Channel `1`, `6`, or `11`.
- [ ] **2.5 AP / Client Isolation Disabled:** Confirm that "AP Isolation", "Client Isolation", or "Guest Network Isolation" is **DISABLED**.
- [ ] **2.6 WAN Disconnected:** Confirm the WAN (Internet) Ethernet port remains completely unplugged.

---

## Phase 3: Mitsubishi PLC Engineering Tool Configuration

### Station 1: Q-Series (`Q03UDECPU`) via GX Works2
- [ ] **3.1 Open Project:** Open active laboratory GX Works2 project.
- [ ] **3.2 Built-in Ethernet Parameter:** Navigate to **Parameter** -> **PLC Parameter** -> **Built-in Ethernet Port Setting**.
  - IP Address: `192.168.3.30` (Confirm whether `.30` or `.39` is programmed: __________)
  - Subnet Mask: `255.255.255.0`
  - Default Router: `192.168.3.1`
- [ ] **3.3 Open Settings:**
  - Protocol: `TCP`
  - Open System: `MC Protocol`
  - Host Station Port No.: `5010` (Decimal)
- [ ] **3.4 Download & Reset:** Download parameters to PLC CPU. Cycle CPU power or execute Reset.
- [ ] **3.5 Laptop Network Ping:** From maintenance laptop (`192.168.3.10`), run:
  - `ping 192.168.3.30` -> Result: [ ] Reply received (<1 ms)
  - `Test-NetConnection -ComputerName 192.168.3.30 -Port 5010` -> Result: [ ] TcpTestSucceeded: True

### Station 2: iQ-R Series (`R04CPU / R04ENCPU`) via GX Works3
- [ ] **3.6 Hardware Suffix Identification:** Inspect CPU rating plate. Record exact model:
  - Exact Model: `[ ] R04CPU  [ ] R04ENCPU  [ ] Other: ____________`
- [ ] **3.7 Ethernet Parameter:** Navigate to **Module Parameter (Ethernet Port)** in GX Works3.
  - IP Address: `192.168.3.20`
  - Subnet Mask: `255.255.255.0`
  - Communication Data Code: **Binary Code**
- [ ] **3.8 SLMP Connection:** Configure SLMP Connection on Port `5010`. Download to CPU and reset.
- [ ] **3.9 Laptop Network Ping:** From maintenance laptop:
  - `ping 192.168.3.20` -> Result: [ ] Reply received
  - `Test-NetConnection -ComputerName 192.168.3.20 -Port 5010` -> Result: [ ] TcpTestSucceeded: True

---

## Phase 4: ESP32 Gateway Firmware Flashing

- [ ] **4.1 Configure Secrets:** Ensure `src/secrets.h` is created with Wi-Fi SSID `RUBY` and matching password.
- [ ] **4.2 Firmware Upload:** Run `pio run -t upload` (or Arduino IDE Upload).
- [ ] **4.3 Filesystem Upload:** Run `pio run -t uploadfs` (uploading `data/` directory to LittleFS).
- [ ] **4.4 Serial Monitor Inspection:** Connect terminal at 115200 baud. Confirm clean boot sequence:
  - [ ] LittleFS mounted successfully
  - [ ] Wi-Fi connected to `RUBY`
  - [ ] Assigned DHCP IP recorded: `192.168.3.______`
  - [ ] HTTP WebServer listening on Port 80
- [ ] **4.5 I2C LCD Verification:** Confirm 16x2 LCD shows:
  - Line 1: `IP:192.168.3.xxx`
  - Line 2: `Q:OK R:OK` (or `R:--` if R-PLC is unpowered)

---

## Phase 5: End-to-End Operational Validation

- [ ] **5.1 Mobile Wi-Fi Association:** Connect smartphone to Wi-Fi `RUBY`. When prompted "Wi-Fi has no internet", select **"Keep Trying Wi-Fi"** / **"Stay Connected"**.
- [ ] **5.2 Dashboard Loading:** In mobile browser, navigate to `http://<ESP32-IP>/`.
  - [ ] Page loads in < 1.0 second.
  - [ ] Dark slate theme renders without layout distortion.
  - [ ] Dual-Station Deck displays Station 1 (`Q03UDECPU`) and Station 2 (`R04CPU`).
- [ ] **5.3 Passive Telemetry Verification (Input X3):**
  - Depress physical pushbutton `X3` on the Q-PLC rack.
  - [ ] Input LED `X3` illuminates on PLC rack.
  - [ ] Dashboard tile `X3` transitions to Green (ON).
  - [ ] Ladder rung diagram displays energized contact.
  - [ ] Event Journal records `STATE_CHANGE: X3 -> ON`.
- [ ] **5.4 Passive Ladder Execution (Output Y20):**
  - While holding `X3`, observe output relay `Y20`.
  - [ ] Output relay LED `Y20` illuminates on PLC rack.
  - [ ] Dashboard tile `Y20` transitions to Green (ON).
  - [ ] Live Read-Back display in Control tab shows `ON (Closed / Energized)`.
- [ ] **5.5 Input Release:** Release pushbutton `X3`.
  - [ ] LEDs extinguish on rack; dashboard tiles `X3` and `Y20` return to Dark Gray (OFF).
- [ ] **5.6 Station Switching:** Tap Station 2 (`melsec_r04`) card on Overview.
  - [ ] Station 2 card highlights.
  - [ ] Header badge updates to R04.
  - [ ] Control tab displays "Outputs Locked (Pending Lab Commissioning)" banner.
- [ ] **5.7 Output Safety Rejection Test:**
  - Select Station 1. Enter invalid PIN `9999`. Click **Set Y20 ON**.
  - [ ] Stage 1 fails with "Authorization Failed".
  - [ ] No relay click or physical state change occurs.
- [ ] **5.8 Authorized Control Actuation Test:**
  - Enter authorized PIN `1234`. Click **Set Y20 ON**.
  - [ ] 5-Stage Verification Pipeline completes with 5 Green checks.
  - [ ] Relay `Y20` clicks on rack (if ladder logic allows).
  - [ ] Event Journal logs actuation with operator verification timestamp.
- [ ] **5.9 Fault Isolation Test:**
  - Unplug Ethernet cable from Station 2 (R-PLC).
  - [ ] Station 2 badge updates to "Offline" or "Provisional".
  - [ ] Station 1 (Q-PLC) continues reading `X3` and `Y20` with zero interruption or lag.

---

## Phase 6: Commissioning Sign-Off

| Milestone | Status | Date | Lead Engineer Signature |
|---|---|---|---|
| Electrical Safety & 5.0V Buck Calibration | [ ] PASSED | ____________ | ___________________________ |
| Dual-PLC Network Infrastructure | [ ] PASSED | ____________ | ___________________________ |
| ESP32 Gateway & I2C LCD | [ ] PASSED | ____________ | ___________________________ |
| Passive Telemetry (X0..X7, Y20..Y27) | [ ] PASSED | ____________ | ___________________________ |
| Supervisory Actuation & Read-Back Verification | [ ] PASSED | ____________ | ___________________________ |
| System Acceptance & Laboratory Handover | [ ] ACCEPTED | ____________ | ___________________________ |
