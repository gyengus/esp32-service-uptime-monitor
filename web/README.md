# ESP32 Service Uptime Monitor - Web Frontend

This directory contains the client-side Web User Interface, static assets, and automated frontend test suites for the ESP32 Service Uptime Monitor.

---

## Architecture Overview

The web application is designed to be lightweight, responsive, and completely self-contained for microcontrollers with limited flash and RAM. It relies on standard browser technologies without heavy single-page application (SPA) runtime frameworks, ensuring rapid load times directly from LittleFS or MicroSD storage.

### Directory Structure

```text
web/
├── index.html            # Main monitoring dashboard (live status, cards, log viewer)
├── config.html           # System configuration, entity management, backups, and OTA
├── fileManager.html      # Filesystem file browser (browse, upload, download, delete)
├── static/
│   ├── app.js            # Main dashboard controller & WebSocket client
│   ├── app.config.js     # Settings form handling, REST API calls, entity CRUD
│   ├── style.css         # Dark/modern responsive theme styles
│   ├── bootstrap.min.css # Bootstrap 5.3 CSS
│   ├── bootstrap.min.js  # Bootstrap 5.3 JS
│   ├── jquery-3.7.1.min.js # jQuery runtime
│   └── favicon.ico       # Application icon
├── tests/
│   ├── setup.js          # Test environment bootstrap (JSDOM, mocks)
│   ├── app.test.js       # Unit tests for dashboard & WebSocket handling
│   ├── app.config.test.js# Unit tests for config forms, backups, OTA, and API handlers
│   └── fileManager.test.js # Unit tests for File Manager UI and operations
├── package.json          # Node.js dev dependencies and test runner scripts
└── vitest.config.js      # Vitest configuration for unit testing and coverage
```

---

## Features & Pages

### 1. Dashboard (`index.html` & `app.js`)
- **Real-Time Monitoring**: Live status updates received over WebSocket (`/ws`) without polling.
- **Service Cards & CRUD**: Displays service state (`ONLINE`, `OFFLINE`, or `DISABLED`), target URL, notification badge, and last state transition timestamp. When configuration mode is unlocked, allows adding, editing, and deleting monitored entities with live icon search across Material Design Icons (`mdi-*`) and Dashboard Icons (`di:*`).
- **Live System Telemetry**: Displays device uptime, Wi-Fi RSSI signal strength, free heap, chip core temperature, firmware version/commit link, and a detailed system diagnostics modal (`/api/sysinfo/details`).
- **Interactive Controls**: Filter services by status (`ALL` / `ONLINE` / `OFFLINE`), view configuration mode countdown badge (`10:00`), and toggle the live WebSocket device log drawer with log level selector.

### 2. Configuration (`config.html` & `app.config.js`)
- **Access Protection**: Configuration routes (`/config`) require physical BOOT button authorization on the ESP32 (or initial AP setup mode).
- **Wi-Fi & Network Management**: Interactive non-blocking Wi-Fi scan, network selection, RFC 1123 mDNS hostname sanitization, and SNTP time synchronization settings.
- **Discord Alerts & Hardware**: Configure Discord webhook URL with live test notification, OTA authentication token, and onboard RGB LED brightness slider with live cyan pulse preview.
- **Backup, Restore & Factory Reset**: Export and import full configuration and monitored entity snapshots as JSON, or trigger a factory reset.
- **Firmware OTA & Web UI Archive Upload**: Flash compiled `.bin` firmware images with MD5 verification (`/ota/start`, `/ota/upload`) or upload `web.zip` packages (`/api/web/upload-archive`) over HTTP.

### 3. File Manager (`fileManager.html`)
- Integrated with the backend `FileManager` library (can be excluded in release builds via `DISABLE_FILE_MANAGER=1` or `RELEASE=1`).
- Direct directory browsing, folder creation, file upload, download, and deletion on the storage backend (Internal LittleFS or MicroSD card).

---

## Testing & Quality Assurance

The frontend includes a comprehensive Vitest unit testing suite utilizing `jsdom` to simulate the browser DOM, fetch API, and WebSocket server responses.

### Running Tests

Ensure dependencies are installed:
```bash
cd web
npm install
```

Run test suite:
```bash
npm test
```

Run tests with code coverage analysis:
```bash
npm run test:coverage
```

### Test Coverage Highlights
- **`app.test.js`**: Validates WebSocket connection lifecycle, entity state rendering, CRUD modals, icon search, status filtering, log streaming, and system info updates.
- **`app.config.test.js`**: Validates configuration form handling, Wi-Fi scan polling, Discord webhook testing, configuration backup/restore, firmware OTA and web archive uploads, LED brightness testing, and error handling toasts.
- **`fileManager.test.js`**: Validates directory listing, breadcrumb navigation, folder creation, file upload/download, and deletion flows.

---

## Build, Minification & Deployment

Frontend assets are bundled and minified automatically by the PlatformIO build chain:

1. **Minification**:
   - During firmware build (`pio run`), [`scripts/web_packer.py`](../scripts/web_packer.py) minifies HTML, CSS, and JS (using Terser when available).
   - Generates an internal metadata `.version` file populated with `${FIRMWARE_VERSION}-${COMMIT_HASH}`.
2. **Firmware Embedding**:
   - Bundled into a compressed in-memory ZIP byte array in [`src/EmbeddedWebArchive.cpp`](../src/EmbeddedWebArchive.cpp) stored in `PROGMEM`.
   - On first boot or firmware update, the ESP32 extracts the embedded web assets to LittleFS/SD automatically.
3. **Standalone Archive (`web.zip`)**:
   - `web.zip` is simultaneously saved in the project root and copied to `dist/web.zip` for GitHub releases.
   - You can also manually package assets using:
     ```bash
     pio run -t packweb
     ```
   - Or deploy directly to an active device over Wi-Fi:
     ```bash
     pio run -t uploadweb
     ```
