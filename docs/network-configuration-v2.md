# Network Configuration & Topology Specification
## Project V2 — Q/R Series Dual-PLC Local Supervisory Network

---

## 1. Network Overview & Design Philosophy

The Project V2 network is an isolated, autonomous, air-gapped industrial Ethernet/Wi-Fi Local Area Network (LAN).
It has zero dependency on internet connectivity, external DNS servers, cloud relays, or enterprise infrastructure.
The network is anchored by a standalone TP-Link wireless router, designated **RUBY**.

---

## 2. Complete IP Address Register

| Device Identity | Hardware Model | Connection Type | IP Address | Subnet Mask | Default Gateway | Port(s) | Role & Protocol |
|---|---|---|---|---|---|---|---|
| **TP-Link Router** | TP-Link RUBY | Internal Switch / AP | `192.168.3.1` | `255.255.255.0` | `192.168.3.1` | 80/TCP | Local DHCP Server & Default Gateway |
| **Maintenance Laptop** | Engineering PC | Wi-Fi / Ethernet | `192.168.3.10` | `255.255.255.0` | `192.168.3.1` | 8000/TCP | GX Works2/3 Programming, CI/CD, Python Agent |
| **Station 2 (R-PLC)** | R04CPU / R04ENCPU | Ethernet (RJ45) | `192.168.3.20` | `255.255.255.0` | `192.168.3.1` | 5010/TCP | SLMP / MC Protocol 3E Binary Server |
| **Station 1 (Q-PLC)** | Q03UDECPU | Ethernet (RJ45) | `192.168.3.30` | `255.255.255.0` | `192.168.3.1` | 5010/TCP | MC Protocol 3E Binary Server |
| **Embedded Gateway** | ESP32 DevKit | Wi-Fi 2.4 GHz | `192.168.3.x` (DHCP) | `255.255.255.0` | `192.168.3.1` | 80/TCP | Central HTTP HMI & REST API Server |
| **Mobile Clients** | Smartphones (iOS/Android) | Wi-Fi 2.4 GHz | `192.168.3.x` (DHCP) | `255.255.255.0` | `192.168.3.1` | Dynamic | Web Browser HMI Clients |

### 2.1 Critical Address Clarification: Historical vs. Active Q-Series IP
> [!IMPORTANT]
> - **Historical Address:** Earlier project revisions referenced `192.168.3.39`.
> - **Current Working Address:** The working configuration provided by the project owner specifies `192.168.3.30`.
> - **Verification Mandate:** All software components in Project V2 default to `192.168.3.30`. During physical laboratory commissioning, test engineers must verify whether the Q03UDECPU built-in Ethernet parameter in GX Works2 is configured for `.30` or `.39` and ensure the router ARP table matches.

---

## 3. Router Configuration Checklist (TP-Link RUBY)

When setting up the physical RUBY router in the laboratory, apply the following settings in the router administrative interface (`http://192.168.3.1` or default `http://192.168.0.1` prior to reconfiguration):

### 3.1 LAN & DHCP Settings
1. **LAN IP Address:** Set to `192.168.3.1`.
2. **Subnet Mask:** Set to `255.255.255.0`.
3. **DHCP Server:** Enabled.
4. **DHCP IP Pool:** `192.168.3.100` to `192.168.3.200` (reserving `.1` through `.99` for static automation hardware).
5. **Lease Time:** 1440 minutes (24 hours).
6. **Primary DNS:** `192.168.3.1` (or leave empty; local resolution only).
7. **Address Reservation (Recommended):**
   - Bind the MAC address of the ESP32 DevKit to static reservation: `192.168.3.50`.
   - Bind the MAC address of the engineering laptop to static reservation: `192.168.3.10`.

### 3.2 Wireless (Wi-Fi) Settings
1. **Wireless Radio:** Enabled (2.4 GHz band).
2. **Network Mode:** 802.11b/g/n mixed.
3. **SSID Name:** `RUBY` (case-sensitive).
4. **Channel Width:** `20 MHz` (strongly recommended over 40 MHz to minimize packet loss and co-channel interference for the ESP32 transceiver).
5. **Channel Selection:** Fixed Channel `1`, `6`, or `11` (avoid "Auto" channel hopping to ensure zero Wi-Fi reconnect drops).
6. **Security Type:** WPA2-PSK (AES / CCMP encryption).
7. **Pre-Shared Key (Password):** Match the project secret configured in `src/secrets.h`.

### 3.3 Isolation and Firewall Rules (CRITICAL FOR COMMISSIONS)
> [!CAUTION]
> **AP Isolation / Client Isolation MUST BE DISABLED.**
> If "AP Isolation", "Client Isolation", or "Guest Network Isolation" is enabled on the router, wireless clients (such as mobile phones) will be blocked from communicating directly with the ESP32 or PLC Ethernet ports. 
> Ensure the mobile phones, ESP32, and physical Ethernet LAN ports reside in the same flat broadcast domain without L2 isolation.

---

## 4. Physical Port & Cable Interconnections

```text
               TP-Link Router ("RUBY")
  ┌────────────────────────────────────────────────────────┐
  │ [WAN/Internet]  [LAN 1]    [LAN 2]    [LAN 3]    [LAN 4]│
  └───────┬────────────┬──────────┬──────────┬─────────────┘
          │            │          │          │
     (UNPLUGGED)       │          │          │
   Air-Gapped LAN      │          │          │
                       │          │          └─ Maintenance Laptop (Optional)
                       │          │             (RJ45 CAT5e / 192.168.3.10)
                       │          │
                       │          └─ Mitsubishi iQ-R Series PLC
                       │             (RJ45 CAT5e / 192.168.3.20)
                       │
                       └─ Mitsubishi Q-Series PLC
                          (RJ45 CAT5e / 192.168.3.30)
```

1. **WAN Port:** Must remain unplugged. Industrial networks must never bridge uncontrolled internet traffic into PLC CPU Ethernet backplanes.
2. **Cables:** Use industrial CAT5e or CAT6 shielded twisted pair (STP) Ethernet cables with grounded RJ45 connectors to prevent electromagnetic noise from nearby VFDs or contactors.
3. **Wi-Fi Range:** Ensure the router is located within 5 meters line-of-sight of the ESP32 DevKit to maintain signal strength > -65 dBm.

---

## 5. Network Troubleshooting Commands

Run from the engineering workstation connected to RUBY:

```powershell
# 1. Verify workstation IP and gateway
ipconfig /all

# 2. Ping Router Gateway
ping -n 4 192.168.3.1

# 3. Ping Mitsubishi Q-Series PLC
ping -n 4 192.168.3.30

# 4. Ping Mitsubishi iQ-R Series PLC
ping -n 4 192.168.3.20

# 5. Ping ESP32 Gateway (replace with assigned DHCP IP)
ping -n 4 192.168.3.50

# 6. Test MC Protocol Port Accessibility on Q-Series
Test-NetConnection -ComputerName 192.168.3.30 -Port 5010

# 7. Test MC Protocol Port Accessibility on R-Series
Test-NetConnection -ComputerName 192.168.3.20 -Port 5010

# 8. Test ESP32 HTTP Server Accessibility
Test-NetConnection -ComputerName 192.168.3.50 -Port 80
```
