# Offline Operation & Standalone Local Execution Guide
## Project V2 — Air-Gapped Industrial Supervisory System

---

## 1. Offline Philosophy & System Independence

Project V2 is explicitly designed to operate in severe industrial field conditions where external internet access is non-existent, forbidden by cybersecurity policy, or disrupted.
The system possesses **zero runtime dependencies** on:
- Internet connectivity
- Cloud MQTT brokers (EMQX Cloud, HiveMQ, AWS IoT)
- External DNS services
- Windows gateway Python applications
- Dedicated laptop computers during routine operation

Once programmed and powered, the hardware assembly operates autonomously as a self-contained embedded supervisory node.

---

## 2. Standard Field Operating Procedure (Cold Start)

To power on and access the system in a production or laboratory setting, follow these steps in order:

```text
  STEP 1: Power ON Router "RUBY" (Wait 30s for 2.4 GHz radio initialization)
     │
  STEP 2: Power ON Mitsubishi Q-Series & iQ-R Series PLCs
     │
  STEP 3: Power ON ESP32 DevKit (5V DC via USB or regulated industrial supply)
     │
  STEP 4: ESP32 automatically associates with "RUBY" via 2.4 GHz Wi-Fi
     │
  STEP 5: ESP32 displays assigned IP on 16x2 LCD (e.g., 192.168.3.155)
     │
  STEP 6: Operator connects smartphone or laptop to Wi-Fi SSID "RUBY"
     │
  STEP 7: Operator opens web browser and navigates to http://192.168.3.155/
     │
  STEP 8: Supervisory Dashboard loads instantly from ESP32 LittleFS flash
     │
  STEP 9: Real-time telemetry streaming begins immediately
```

---

## 3. Communication Isolation & Failure Tolerance

The system architecture implements complete fault isolation between all physical and logical layers:

### 3.1 Dual-PLC Isolation
- **Independent Sockets:** Station 1 (Q03) and Station 2 (R04) are polled on separate non-blocking sockets with independent state machines.
- **Fault Independence:** If Station 2 is powered off, disconnected, or uncommissioned:
  - Station 2 status transitions to `COMM_FAILURE / OFFLINE`.
  - Station 1 continues polling, updating, and accepting verified commands at full speed.
  - The gateway CPU and web server remain fully responsive.

### 3.2 Client Isolation
- The ESP32 web server serves clients using cooperative event-driven HTTP connections.
- Multiple operators can view the dashboard simultaneously from separate mobile phones.
- An operator closing their browser or losing Wi-Fi signal has zero effect on background PLC polling or other active operators.

---

## 4. Development & Fallback Modes

While the embedded ESP32 gateway is the primary operating platform, two fallback modes are built into the architecture:

### 4.1 Fallback Mode A: Workstation Python Agent
If the ESP32 is being reprogrammed or undergoing maintenance, an engineering laptop can run the local Python agent:
```powershell
# Run the local agent on the RUBY LAN:
.venv\Scripts\python.exe -m agent.cli --run --allow-remote --host 192.168.3.10 --port 8000
```
Operators can navigate to `http://192.168.3.10:8000/` and access the exact same UI, telemetry endpoints, and dual-station diagnostics.

### 4.2 Fallback Mode B: Deterministic Browser Simulation (Demo Mode)
If no PLC hardware, router, or gateway is available (e.g. during frontend design review or classroom training):
1. Open `web/index.html` directly in any web browser (or over `file://`).
2. Toggle the **"DEMO SIMULATION"** switch in the Settings tab.
3. The browser state machine simulates:
   - Periodic 2-second pulse cycles across independent digital inputs `X0` through `X7`.
   - Independent output states across `Y20` through `Y27` (uncoupled, no ladder relationship).
   - Simulated 5-stage output verification pipeline for all 8 outputs with PIN authorization.
   - Jitter, sequence gap, and stale telemetry test scenarios.
