# Physical Laboratory Commissioning & Electrical Safety Guide
## Step-by-Step Procedure for On-Site Hardware Integration

---

## 1. Safety Warning & Electrical Prerequisites

> [!CAUTION]
> **CRITICAL ELECTRICAL HAZARD: 24V DC to 5V DC Step-Down Verification**
> Industrial control cabinets typically operate on **24V DC**.
> Connecting 24V DC directly to the ESP32 DevKit (rated for 5.0V VIN or 3.3V VCC) will cause catastrophic component destruction, fire hazard, and electrical failure.
>
> **Mandatory Verification Protocol Before Connecting the ESP32:**
> 1. Wire the 24V DC power supply to the **Input** terminals of the DC-DC Buck Converter (e.g. LM2596).
> 2. Leave the **Output** terminals completely disconnected from the ESP32.
> 3. Power on the 24V DC supply.
> 4. Use a calibrated Digital Multimeter (DMM) set to DC Volts to measure across the buck converter output terminals.
> 5. Turn the brass potentiometer screw until the DMM reads **exactly 5.00V DC (acceptable range: 4.95V to 5.05V)**.
> 6. Power off the 24V DC supply.
> 7. Connect the buck converter 5.0V Output to ESP32 **VIN** and **GND**.
> 8. Re-verify voltage with the DMM after connecting the load.

---

## 2. Mitsubishi PLC Engineering Tool Configuration

Before connecting the ESP32 to the PLCs, verify the Ethernet port configurations in the Mitsubishi programming tools:

### 2.1 Station 1: Q-Series (`Q03UDECPU`) in GX Works2
1. Open the active GX Works2 project.
2. In the Project tree, navigate to:  
   **Parameter** -> **PLC Parameter** -> **Built-in Ethernet Port Setting**.
3. **IP Address Configuration:**
   - IP Address: `192.168.3.30` *(Note: If the physical project is programmed for `192.168.3.39`, update the project to `.30` or adjust `config/devices.yaml` and `src/config.h` accordingly)*.
   - Subnet Mask: `255.255.255.0`.
   - Default Router IP: `192.168.3.1`.
4. **Open Settings (MC Protocol Port):**
   - Click **Open Setting**.
   - Protocol: `TCP`
   - Open System: `MC Protocol`
   - Fixed Buffer: (uncheck)
   - Fixed Buffer Comm.: (uncheck)
   - Pairing Open: `Disable`
   - Existence Confirmation: `Confirm`
   - Host Station Port No.: `5010` (Decimal)
5. Click **End** -> **Check** -> **Download Parameter to PLC CPU**.
6. Cycle the PLC CPU power or perform a CPU Reset.

### 2.2 Station 2: iQ-R Series (`R04CPU / R04ENCPU`) in GX Works3
1. Open the active GX Works3 project.
2. Navigate to:  
   **Parameter** -> **Module Information** -> **CPU Module** -> **Module Parameter (Ethernet Port)**.
3. **Application Settings -> IP Address Setting:**
   - IP Address: `192.168.3.20`
   - Subnet Mask: `255.255.255.0`
   - Default Gateway: `192.168.3.1`
4. **Communication Data Code:**
   - Select **Binary Code** (do NOT select ASCII).
5. **External Device Configuration (SLMP Connection):**
   - Under **Detailed Setting**, add an **SLMP Connection** on Port `5010`.
6. Apply settings, rebuild project, and write parameters to the CPU module.
7. Reset the CPU.

---

## 3. Network Infrastructure Commissioning

1. **Mount Router RUBY:** Place within 3 meters of the PLC rack and ESP32.
2. **Connect Ethernet Cables:**
   - Q03UDECPU Built-in Ethernet -> RUBY LAN Port 1.
   - R04CPU Built-in Ethernet -> RUBY LAN Port 2.
   - Engineering Laptop -> RUBY LAN Port 3 (or connect to Wi-Fi SSID `RUBY`).
3. **Verify Connectivity from Laptop (`192.168.3.10`):**
   ```powershell
   # Ping Q-Series PLC
   ping 192.168.3.30
   
   # Ping R-Series PLC
   ping 192.168.3.20
   
   # Verify Q-Series MC Protocol port is listening
   Test-NetConnection -ComputerName 192.168.3.30 -Port 5010
   
   # Verify R-Series MC Protocol port is listening
   Test-NetConnection -ComputerName 192.168.3.20 -Port 5010
   ```

---

## 4. ESP32 Gateway Commissioning

1. Flash the ESP32 firmware and LittleFS filesystem (see `docs/esp32-firmware-setup.md`).
2. Power on the ESP32 DevKit.
3. Observe the 16x2 I2C LCD:
   - Line 1 displays: `IP:192.168.3.xxx`
   - Line 2 displays: `Q:OK R:OK`
4. If Line 2 displays `Q:ERR`:
   - Check Ethernet cable between Q-PLC and router.
   - Verify Q-PLC Built-in Ethernet Open Settings (Port 5010 TCP MC Protocol).
   - Check if Q-PLC IP address in GX Works2 is `.30` or `.39`.

---

## 5. End-to-End Operational Validation Checklist

| Test Step | Action | Expected Physical Result | Expected Dashboard Result | Verified |
|---|---|---|---|---|
| **1. Mobile Link** | Connect smartphone to Wi-Fi `RUBY` and browse to `http://192.168.3.x/` | Dashboard loads under 1 second | Station 1 & 2 cards show "Online", Subsystem pills show "Agent Running" | [ ] |
| **2. Passive Read (X3)** | Press physical pushbutton wired to input `X3` on Q-PLC | Input LED `X3` illuminates on CPU rack | Dashboard tile `X3` immediately turns Green (ON); Ladder diagram contact illuminates | [ ] |
| **3. Passive Ladder (Y20)** | While holding `X3`, observe output relay `Y20` | Output LED `Y20` illuminates on Q-PLC rack | Dashboard tile `Y20` turns Green (ON); Ladder coil energizes; Live Read-Back shows ON | [ ] |
| **4. Release Input** | Release pushbutton `X3` | LED `X3` and `Y20` extinguish | Tiles `X3` and `Y20` return to Dark Gray (OFF); Live Read-Back shows OFF | [ ] |
| **5. Station Switching** | Tap Station 2 (`melsec_r04`) card | Station 2 highlights | Overview switches to R04; Control tab displays "Outputs Locked (Provisional)" banner | [ ] |
| **6. Output Safety Check** | Switch back to Station 1, enter invalid PIN `9999`, click Set Y20 ON | No physical output change | Stage 1 passes, Stage 2 fails with "Authorization Failed (401)" | [ ] |
| **7. Authorized Actuation** | Enter PIN `1234`, click Set Y20 ON | Output relay `Y20` clicks on rack (if ladder allows) | 5-stage pipeline completes with 5 Green checks; Event Journal logs actuation | [ ] |
| **8. Fault Isolation** | Disconnect Ethernet cable from R-PLC (Station 2) | R-PLC rack disconnected | Station 2 shows "Offline"; Station 1 continues monitoring X3 and Y20 with zero interruption | [ ] |
