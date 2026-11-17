# Dual-PLC Protocol Specification: Mitsubishi Q-Series & iQ-R Series
## MC Protocol 3E Binary Frame Architecture, Subcommands, and Ladder Scans

---

## 1. Protocol Architecture & Overview

Mitsubishi Electric MELSEC PLCs utilize **MC Protocol** (MELSEC Communication Protocol) or **SLMP** (Seamless Message Protocol) for communication over Ethernet TCP/IP.
The communication model utilizes **3E Binary Frames** for maximum throughput, lowest transport overhead, and deterministic binary parsing on microcontrollers.

Project V2 communicates simultaneously with two distinct Mitsubishi architectures:
1. **Station 1 (MELSEC-Q):** Q03UDECPU via MC Protocol 3E Binary (Subcommand `0x0001`, 3-byte address, 1-byte code).
2. **Station 2 (MELSEC iQ-R):** R04CPU / R04ENCPU via SLMP / MC Protocol 3E Binary (Subcommand `0x0003`, 4-byte address, 2-byte code).

---

## 2. MC Protocol 3E Binary Frame Structure

### 2.1 Q-Series Frame Construction (MELSEC-Q03UDECPU)

#### A. 3E Batch Read Request (Command 0x0401, Subcommand 0x0001)
Used to read inputs `X0..X7` and outputs `Y20..Y27`.

| Byte Offset | Field Name | Length | Value (Hex) | Description |
|---|---|---|---|---|
| 0 .. 1 | Subheader | 2 bytes | `0x50, 0x00` | 3E frame request identifier |
| 2 | Network No. | 1 byte | `0x00` | Local network (0x00 = self station) |
| 3 | PC No. | 1 byte | `0xFF` | Target CPU (0xFF = direct CPU) |
| 4 .. 5 | Target Module I/O | 2 bytes | `0xFF, 0x03` | Default built-in CPU Ethernet (`0x03FF`) |
| 6 | Target Station No. | 1 byte | `0x00` | Multidrop station number |
| 7 .. 8 | Request Data Length | 2 bytes | `0x0C, 0x00` | Remaining bytes: 12 bytes (`0x000C`) |
| 9 .. 10 | CPU Monitoring Timer | 2 bytes | `0x10, 0x00` | 16 units of 250 ms (4.0 seconds timeout) |
| 11 .. 12 | Command | 2 bytes | `0x01, 0x04` | Batch Read Command (`0x0401`) |
| 13 .. 14 | Subcommand | 2 bytes | `0x01, 0x00` | Bit Units Subcommand (`0x0001`) |
| 15 .. 17 | Head Device Number | 3 bytes | `0x00, 0x00, 0x00` | Device starting address (3 bytes, little-endian) |
| 18 | Device Code | 1 byte | `0x9C` or `0x9D` | `0x9C` = Input (X), `0x9D` = Output (Y) |
| 19 .. 20 | Number of Points | 2 bytes | `0x08, 0x00` | 8 points (`0x0008`, little-endian) |

*Total Request Frame Length:* 21 bytes.

#### B. 3E Batch Write Request (Command 0x1401, Subcommand 0x0001)
Used to actuate authorized output `Y20`.

| Byte Offset | Field Name | Length | Value (Hex) | Description |
|---|---|---|---|---|
| 0 .. 1 | Subheader | 2 bytes | `0x50, 0x00` | 3E frame request identifier |
| 2 .. 6 | Routing (Net/PC/IO/Stn) | 5 bytes | `0x00, 0xFF, 0xFF, 0x03, 0x00` | Standard routing header |
| 7 .. 8 | Request Data Length | 2 bytes | `0x0D, 0x00` | Remaining bytes: 13 bytes (`0x000D`) |
| 9 .. 10 | CPU Monitoring Timer | 2 bytes | `0x10, 0x00` | 4.0 seconds monitoring timer |
| 11 .. 12 | Command | 2 bytes | `0x01, 0x14` | Batch Write Command (`0x1401`) |
| 13 .. 14 | Subcommand | 2 bytes | `0x01, 0x00` | Bit Units Subcommand (`0x0001`) |
| 15 .. 17 | Head Device Number | 3 bytes | `0x20, 0x00, 0x00` | Address: Y20 (0x000020, little-endian) |
| 18 | Device Code | 1 byte | `0x9D` | Output Relay (`0x9D` = Y) |
| 19 .. 20 | Number of Points | 2 bytes | `0x01, 0x00` | 1 point (`0x0001`, little-endian) |
| 21 | Write Bit Data | 1 byte | `0x10` or `0x00` | **High nibble:** `0x10` = ON, `0x00` = OFF |

*Total Write Frame Length:* 22 bytes.

#### C. Response Frame Parsing (Both Read & Write)
The PLC CPU responds with an 11-byte header plus data:

| Byte Offset | Field Name | Length | Value (Hex) | Description |
|---|---|---|---|---|
| 0 .. 1 | Subheader | 2 bytes | `0xD0, 0x00` | 3E response identifier |
| 2 .. 6 | Routing Header | 5 bytes | `0x00, 0xFF, 0xFF, 0x03, 0x00` | Mirrored routing information |
| 7 .. 8 | Response Data Length | 2 bytes | Variable | Length of remaining response bytes |
| 9 .. 10 | **End Code** | 2 bytes | `0x00, 0x00` | **Execution Result: `0x0000` = SUCCESS** |
| 11 .. N | Response Data | Variable | Bit Stream | Bit states returned (packed 2 bits per byte) |

*Write Response:* Contains 0 data bytes after the End Code (Total response: 11 bytes).  
*Read Response:* For 8 points in bit units, each byte contains 2 consecutive bits:
- High nibble: first bit (0x1 = ON, 0x0 = OFF)
- Low nibble: second bit (0x1 = ON, 0x0 = OFF)

---

### 2.2 iQ-R Series Frame Construction (MELSEC-R04CPU / R04ENCPU)

The iQ-R Series utilizes extended SLMP 3E frames designed for expanded 32-bit device address spaces.

#### Key Protocol Differences vs. Q-Series:
1. **Subcommand:** `0x0003` (SLMP Bit Units), compared to `0x0001` on Q-Series.
2. **Head Device Number:** **4 bytes** little-endian (e.g. `0x20, 0x00, 0x00, 0x00`), compared to 3 bytes on Q-Series.
3. **Device Code:** **2 bytes** little-endian (`0x9C, 0x00` for X, `0x9D, 0x00` for Y), compared to 1 byte on Q-Series.
4. **Request Data Length:** Increased by 2 bytes due to expanded address and device code fields.

#### A. iQ-R 3E Batch Read Request (23 bytes total):
- Subheader: `0x50, 0x00`
- Routing: `0x00, 0xFF, 0xFF, 0x03, 0x00`
- Data Length: `0x0E, 0x00` (14 bytes)
- Timer: `0x10, 0x00`
- Command: `0x01, 0x04`
- Subcommand: `0x03, 0x00` (`0x0003`)
- Head Device: `0x00, 0x00, 0x00, 0x00` (4 bytes)
- Device Code: `0x9C, 0x00` (2 bytes, Input X)
- Points: `0x08, 0x00` (8 points)

#### B. iQ-R 3E Batch Write Request (24 bytes total):
- Subcommand: `0x03, 0x00`
- Head Device: `0x20, 0x00, 0x00, 0x00` (4 bytes)
- Device Code: `0x9D, 0x00` (2 bytes, Output Y)
- Points: `0x01, 0x00` (1 point)
- Data: `0x10` (ON) or `0x00` (OFF)

---

## 3. End Code Error Reference

When a command cannot be executed, the PLC returns a non-zero End Code at bytes 9-10 of the response frame:

| End Code | Classification | Cause & Diagnostic Action |
|---|---|---|
| `0x0000` | Normal Completion | Command executed successfully. |
| `0xC059` | Command / Subcommand Error | Subcommand does not match CPU family (e.g. sending 0x0001 to iQ-R or 0x0003 to Q-Series). |
| `0xC050` | Device Specification Error | Requested point exceeds device memory bounds or invalid device code. |
| `0xC05B` | Write Protection Error | Output coil is locked by PLC CPU parameter or memory card protection. |
| `0xC061` | Request Length Error | Frame length field does not match the actual byte count received. |
| `0x4000` | Module Not Found | Target I/O number `0x03FF` does not map to built-in Ethernet. |

---

## 4. Independent I/O Operation & Cyclic Scan Architecture (Project V2.2)

> [!NOTE]
> **Superseded Ladder Assumption:**
> Early project documentation hypothesized that an internal ladder rung `LD X003 -> OUT Y020` was active in the Q-Series PLC.
> **Actual Physical Bench Baseline:** As verified on the physical prototype, the bench runs **independent digital I/O**:
> - 8 Physical Inputs: `X0` to `X7` (`0x9C`, offsets `0x00`–`0x07`).
> - 8 Independent Outputs: `Y20` to `Y27` (`0x9D`, hexadecimal offsets `0x20`–`0x27`).
> - No ladder relationship couples input `X3` to output `Y20`.
>
> Each output device point (`Y20` through `Y27`) is commanded individually via MC Protocol 3E binary batch write (`0x1401`, subcommand `0x0001`, device code `0x9D`, 1 point).
> 
> **PLC Scan Cycle Considerations for Future Commissioning:**
> When commissioning on arbitrary PLC installations, any output point driven by a cyclic ladder coil (`OUT Yxx`) will have its memory overwritten by ladder evaluation on the next scan (1–5 ms). To use software actuation on physical outputs, the corresponding coil must either be free of conflicting ladder rungs or controlled via internal supervisory registers (`M` or `D`). In the verified bench demonstration, outputs `Y20` through `Y27` are free of scan conflicts.

---

## 5. Banned Point Y0 Security Specification

In early conceptual documentation, `Y0` was mistakenly shown in UI mockups.
In standard Mitsubishi automation equipment:
- `Y0` is frequently wired as the Master Emergency Circuit, Hydraulic Pump Contactor, or Axis 1 Pulse Output.
- Unintentional toggling of `Y0` represents a physical equipment hazard.

**Enforcement:**
- `mc_control.py`: Rejects `Y0` with `ForbiddenPointError` before socket dispatch.
- `mc_protocol.cpp`: Rejects `Y0` with error code before frame construction.
- `app.js`: Rejects `Y0` in pre-flight safety checks (Stage 1).
- `devices.yaml`: Excludes `Y0` from permitted output allowlists.
