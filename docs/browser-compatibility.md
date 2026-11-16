# Browser Compatibility, Mobile Networking & Security Restrictions
## Local Web Architecture, Mixed Content, Private Network Access (PNA), and CORS

---

## 1. Executive Summary

Project V2 is designed to run directly from an embedded ESP32 web server over an air-gapped local Wi-Fi router (`RUBY`), as well as via an optional hosted static build (e.g. GitHub Pages or Netlify).
Operating modern web browsers across local, air-gapped, and cross-origin environments requires strict adherence to browser security specifications.

---

## 2. Core Browser Security Gates

### 2.1 The Mixed Content Gate (HTTPS vs. HTTP)
- **Problem:** When an application is loaded from an HTTPS origin (e.g. `https://username.github.io`), the browser prohibits plain HTTP AJAX/fetch requests (`http://192.168.3.x/api/v1/...`) due to **Mixed Active Content Blocking**.
- **Impact on Hosted Static Builds:** A dashboard loaded over HTTPS cannot make direct HTTP fetch calls to a local ESP32 gateway IP without browser security intervention.
- **Project V2 Solution:**
  1. **Primary Deployment Path (Zero Mixed Content):** Operators access the dashboard directly from the ESP32 gateway over plain HTTP: `http://192.168.3.x/`. In this environment, both the HTML document and the API endpoints are in the same HTTP origin, resulting in **zero mixed content restrictions**.
  2. **Hosted Static Deployment Path:** When deployed to GitHub Pages or Netlify over HTTPS, the user is provided with clear instructions to either:
     - Deploy over HTTP (e.g. on a local intranet server), or
     - Allow mixed content in browser site permissions for that domain, or
     - Use the local ESP32-hosted version directly.

---

### 2.2 Private Network Access (PNA / RFC 1918)
Modern Chromium browsers (Google Chrome, Microsoft Edge, Opera, Brave) implement the **W3C Private Network Access** specification:
- Requests from a public website (e.g., `github.io`) targeting a private IP address (`192.168.3.0/24`, RFC 1918) trigger an explicit **PNA Preflight Request**:
  - The browser sends an `OPTIONS` request with header `Access-Control-Request-Private-Network: true`.
  - The server must respond with header `Access-Control-Allow-Private-Network: true`.

#### ESP32 Gateway Implementation:
The ESP32 firmware in `web_server.cpp` natively includes all required PNA and CORS headers on both `OPTIONS` and standard responses:
```cpp
server.sendHeader("Access-Control-Allow-Origin", "*");
server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Operator-PIN, X-Agent-API-Key, Access-Control-Request-Private-Network");
server.sendHeader("Access-Control-Allow-Private-Network", "true");
```

---

## 3. Mobile Operating System Specifics

### 3.1 Apple iOS (iPhone / iPad — Safari & Chrome)
When an iPhone connects to the TP-Link `RUBY` network without an internet uplink:
1. **"Wi-Fi Has No Internet" Dialog:**
   - iOS will detect that Apple captive portal endpoints (`captive.apple.com`) do not respond.
   - iOS displays a prompt: *"Wi-Fi Has No Internet Connection"*.
   - **Operator Action:** Select **"Keep Trying Wi-Fi"** or **"Use Wi-Fi Without Internet"**. Do NOT select "Use Cellular Data" (which routes traffic away from the local 192.168.3.x LAN).
2. **Local Network Permission Prompt (iOS 14+):**
   - When accessing the web app or scanning local devices, iOS may display: *"[App/Browser] wants to find and connect to devices on your local network."*
   - **Operator Action:** Tap **"Allow"**. If accidentally dismissed with "Don't Allow", enable it in **Settings -> Safari / Chrome -> Local Network**.

### 3.2 Google Android (Samsung, Pixel, Motorola)
1. **"Wi-Fi has no internet access" Notification:**
   - Android sends an active probing packet to `connectivitycheck.gstatic.com`.
   - When the air-gapped network does not respond, a notification appears: *"Wi-Fi has no internet access. Tap for options."*
   - **Operator Action:** Tap the notification and check **"Always connect"** or **"Stay connected"**. If ignored, Android may automatically switch active data routing to cellular 4G/5G, breaking connectivity to `192.168.3.x`.

---

## 4. Browser Feature Matrix & Verification

| Browser | Platform | Local HTTP (ESP32) | Hosted HTTPS (GitHub Pages) | Notes |
|---|---|---|---|---|
| **Google Chrome Mobile** | Android 10+ | Supported (Full) | Requires PNA headers & mixed content opt-in | Recommended mobile browser |
| **Apple Safari** | iOS 15+ | Supported (Full) | Requires Local Network permission | Ensure "Keep Trying Wi-Fi" is selected |
| **Mozilla Firefox Mobile**| Android | Supported (Full) | Mixed content blocked by default | Excellent CSS Grid rendering |
| **Google Chrome Desktop**| Windows / macOS | Supported (Full) | Supports `#unsafely-treat-insecure-origin-as-secure` | Primary development & testing tool |
| **Microsoft Edge** | Windows 10/11 | Supported (Full) | Same behavior as Chromium | Full compatibility verified |

---

## 5. Caching & Performance Optimization

To ensure instantaneous loading over 2.4 GHz Wi-Fi on the ESP32:
1. **Static Files (HTML, CSS, JS):**
   - Served with `Cache-Control: public, max-age=3600` so mobile browsers cache them after initial load.
2. **API Endpoints (`/api/v1/...`):**
   - Served with `Cache-Control: no-cache, no-store, must-revalidate` and `Pragma: no-cache` to ensure real-time telemetry freshness.
