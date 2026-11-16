# Hosted Static Build & Cloud Deployment Guide
## Deploying the Q/R Series Supervisory Dashboard to GitHub Pages, Netlify & Vercel

---

## 1. Overview of the Hosted Static Distribution

In addition to direct embedded hosting on the ESP32 gateway, Project V2 generates a standalone static distribution located in:

`dist/hosted/`

This distribution contains:
- `index.html`: Fully featured industrial supervisory dashboard with dual-station support, responsive mobile layout, and control actuation panel.
- `test.html`: Automated browser-side test suite executing 148 deterministic unit and integration assertions.
- `assets/css/style.css`: Unified industrial dark and light stylesheets.
- `assets/js/app.js`: Complete supervisory logic, state machines, and API adapters.

All paths in `dist/hosted/` use **relative addressing** (`assets/css/...`, `assets/js/...`) to ensure flawless execution in subdirectories (such as GitHub Pages user/repo URLs).

---

## 2. Deployment Instructions

### 2.1 Deployment to GitHub Pages
1. The repository branch `feature/esp32-local-dual-plc` contains the production assets in `dist/hosted/`.
2. To publish via GitHub Pages (when authorized):
   - In GitHub repository **Settings -> Pages**:
   - Source: Deploy from branch.
   - Branch: `feature/esp32-local-dual-plc`, folder `/dist/hosted`.
   - Click **Save**.
3. Access URL: `https://<organization>.github.io/<repository>/`

### 2.2 Deployment to Netlify
1. Connect repository or use Netlify CLI:
   ```bash
   netlify deploy --prod --dir=dist/hosted
   ```
2. Build Settings:
   - Base directory: (leave empty)
   - Build command: (none required; pre-built static assets)
   - Publish directory: `dist/hosted`

### 2.3 Deployment to Vercel
1. Run Vercel CLI:
   ```bash
   vercel deploy --prod dist/hosted
   ```
2. Or configure `vercel.json` pointing `outputDirectory` to `dist/hosted`.

---

## 3. Configuring Remote Connection to ESP32 / Local Agent

When the dashboard is hosted on an external public HTTPS domain (e.g., GitHub Pages), it cannot automatically guess the local private IP of your laboratory ESP32 gateway.

### Connection Configuration Steps:
1. Open the hosted dashboard URL on your phone or laptop connected to the `RUBY` Wi-Fi network.
2. Navigate to the **Settings** tab.
3. Under **Data Ingestion Source**, select **"Local Ingestion Agent / ESP32 Gateway"**.
4. In the **Gateway Base URL** field, enter your local gateway address:
   - For ESP32 DevKit: `http://192.168.3.x` (or the DHCP IP shown on the 16x2 LCD).
   - For Windows Agent: `http://192.168.3.10:8000` (or `http://localhost:8000`).
5. Click **Connect**.
6. The dashboard stores the configured URL in browser `localStorage` and begins polling `/api/v1/devices` and `/api/v1/telemetry`.

---

## 4. Handling Browser Mixed Content & PNA Restrictions

When accessing an HTTP local gateway (`http://192.168.3.x`) from an HTTPS hosted dashboard (`https://...github.io`), modern web browsers enforce security blocks:

1. **Google Chrome / Microsoft Edge:**
   - Chrome will issue a Private Network Access preflight request. The ESP32 firmware natively responds with `Access-Control-Allow-Private-Network: true`.
   - If mixed content is blocked: Click the **Tune / Site Settings** icon in the address bar -> Under **Insecure content**, select **"Allow"**.
2. **Apple Safari (iOS / macOS):**
   - Ensure the browser is granted "Local Network Access" permission.
3. **Alternative Recommendation:**
   - For operational industrial use, accessing `http://<ESP32-IP>/` directly on the local network avoids all mixed-content and cross-origin restrictions completely.
