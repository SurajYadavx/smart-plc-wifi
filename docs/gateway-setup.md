# Gateway Setup — Windows Laptop (Historical Stage 1.0 Baseline)

> **HISTORICAL NOTICE (SUPERSEDED BY PROJECT V2.2):**  
> In Project V2.2, the central gateway is an embedded ESP32 DevKit connected via Wi-Fi to the `RUBY` router.  
> The Python agent remains available for testing and PC-based supervision. Ladder coupling assumptions are superseded by independent 8-input and 8-output operation.

## Prerequisites

| Requirement | Notes |
|---|---|
| Python 3.11 | Verified: `python --version` |
| PLC reachable at 192.168.3.39:5010 (or .30) | PC must be on PLC LAN |
| NodeMCU / ESP32 on COM port | USB cable connected before starting gateway |
| GX Works2 device verification | Verify inputs X0–X7 and outputs Y20–Y27 |

## Step 1 — Create the virtual environment

```powershell
cd gateway

# Create isolated environment
python -m venv .venv

# Activate it (do this every time you open a new terminal)
.venv\Scripts\activate

# Verify you are inside the venv
where python
# Should show: ...\gateway\.venv\Scripts\python.exe
```

## Step 2 — Install dependencies

```powershell
# With .venv active:
pip install -r requirements.txt
```

Expected output confirms pymcprotocol and pyserial are installed.

## Step 3 — Configure

Review and edit `config\settings.ini` to match your target PLC and serial port allocation. Note that IP addresses and COM ports are dynamic parameters:

```ini
[plc]
# Set to your Mitsubishi Q-Series IP address on the local subnet:
ip   = 192.168.3.39
port = 5010

[serial]
# Set to the COM port assigned by Windows Device Manager:
port = COM9
baud_rate = 115200

[devices]
inputs  = X0,X1,X2,X3,X4,X5,X6,X7
outputs = Y20,Y21,Y22,Y23,Y24,Y25,Y26,Y27
```

If your network uses a different subnet or device layout, edit `settings.ini` accordingly. Never commit `secrets.ini`.

## Step 4 — Smoke-test the PLC connection (read-only)

```powershell
# With .venv active, from the gateway\ directory:
python test_plc_connection.py

# Or specify custom dynamic parameters on the command line:
python test_plc_connection.py --ip <YOUR_PLC_IP> --port 5010 --polls 5
```

Expected successful output:
```
INFO  Python      : 3.11.5 ...
INFO  pymcprotocol: 0.3.x
INFO  PASS - PLC TCP connection established
{
  "timestamp_iso": "2026-10-15T12:00:00Z",
  "inputs":  { "X0":0, ..., "X3":1, ... },
  "outputs": { "Y20":1, ... },
  "connected": true,
  "error": null
}
INFO  Poll 1 OK
[PASS] All polls succeeded - gateway is ready to run.
```

**Do not proceed to running the gateway if this test fails.**

### Troubleshooting test failures

| Error | Likely cause | Fix |
|---|---|---|
| `Connection refused` | PLC off or wrong IP/port | Ping 192.168.3.39; check GX Works2 Ethernet settings |
| `Timed out` | Firewall blocking port 5010 | Add Windows Firewall exception for outbound TCP 5010 |
| `Module not found: pymcprotocol` | Not in venv | Activate venv and re-run pip install |
| `batchread_bitunits error` | Device address wrong | Verify X/Y address in GX Works2 |

## Step 5 — Start the gateway

```powershell
# With .venv active:
python gateway.py
```

The gateway will:
1. Open COM9 at 115200 baud
2. Connect to the PLC
3. Poll every 1 second
4. Write JSON to serial
5. Log to `logs\gateway.log`

To use a custom config file:
```powershell
python gateway.py --config config\settings.ini
```

## Step 6 — Verify serial output

With gateway running, open Arduino IDE Serial Monitor (COM9, 115200 baud)
**before** the ESP8266 is connected — or use PuTTY:

```
putty.exe -serial COM9 -sercfg 115200,8,n,1,N
```

You should see one JSON line per second, e.g.:
```json
{"type":"plc_reading","gw_id":"plc-gw-01","seq":1,"ts":"2025-01-01T12:00:00Z","connected":true,"error":null,"inputs":{"X0":0,"X1":0,"X2":0,"X3":0,"X4":0,"X5":0,"X6":0,"X7":0},"outputs":{"Y20":0,"Y21":0,"Y22":0,"Y23":0,"Y24":0,"Y25":0,"Y26":0,"Y27":0}}
```

## Running at startup (optional)

Create a `.bat` file:

```batch
@echo off
cd /d "D:\Academics\TY - S1\MS\mitsubishi-q-plc-app_ANTIGRAVITY\gateway"
call .venv\Scripts\activate
python gateway.py
pause
```

Place a shortcut in `shell:startup` to run on Windows login.

## Logs

Gateway logs are written to `gateway\logs\gateway.log`.
The file is excluded from Git.  Rotate or clear it periodically.
