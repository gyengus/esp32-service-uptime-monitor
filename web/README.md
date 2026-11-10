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
│   └── app.config.test.js# Unit tests for config forms, CRUD, and API handlers
├── package.json          # Node.js dev dependencies and test runner scripts
└── vitest.config.js      # Vitest configuration for unit testing and coverage
```

---

## Features & Pages

### 1. Dashboard (`index.html` & `app.js`)
- **Real-Time Monitoring**: Live status updates received over WebSocket (`/ws`) without polling.
- **Service Cards**: Displays service state (`ONLINE` / `OFFLINE`), response latency, failure counts, and uptime percentages.
- **Live System Telemetry**: Displays device free heap, uptime counter, chip core temperature, and Wi-Fi RSSI signal strength.
- **Interactive Controls**: Filter services by status, restart device remotely, and open live WebSocket log stream.

### 2. Configuration (`config.html` & `app.config.js`)
- **Access Protection**: Configuration routes (`/config`) require physical BOOT button authorization on the ESP32.
- **Wi-Fi Management**: Interactive non-blocking Wi-Fi scan and network selection.
- **Monitored Entities CRUD**: Add, edit, test, reorder, and remove HTTP/HTTPS endpoints with custom Material Design icons and notification flags.
- **Discord Alerts**: Configure webhook URL and test notifications directly from the browser.
- **Backup & Restore**: Export and import full configuration and monitored entity snapshots as JSON.
- **Web UI Archive Upload**: Upload new `web.zip` packages over HTTP to update frontend assets without flashing firmware.

### 3. File Manager (`fileManager.html`)
- Integrated with the backend `FileManager` library.
- Direct file upload, download, and deletion on the storage backend (Internal LittleFS or MicroSD card).

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
- **`app.test.js`**: Validates WebSocket connection lifecycle, entity state rendering, latency badge coloring, status transitions, and system info updates.
- **`app.config.test.js`**: Validates form validation, entity add/edit/delete flows, Wi-Fi scan polling, configuration backup/restore, LED brightness testing, and error handling toasts.

---

## Build, Minification & Deployment

Frontend assets are bundled and minified automatically by the PlatformIO build chain:

1. **Minification**:
   - During firmware build (`pio run`), [`scripts/web_packer.py`](file:///home/gyengus/dev/esp32-service-uptime-monitor/scripts/web_packer.py) minifies HTML, CSS, and JS (using Terser when available).
   - Generates an internal metadata `.version` file populated with `${FIRMWARE_VERSION}-${COMMIT_HASH}`.
2. **Firmware Embedding**:
   - Bundled into a compressed in-memory ZIP byte array in [`src/EmbeddedWebArchive.cpp`](file:///home/gyengus/dev/esp32-service-uptime-monitor/src/EmbeddedWebArchive.cpp) stored in `PROGMEM`.
   - On first boot or firmware update, the ESP32 extracts the embedded web assets to LittleFS/SD automatically.
3. **Standalone Archive (`web.zip`)**:
   - `web.zip` is simultaneously saved in the project root and copied to [`dist/web.zip`](file:///home/gyengus/dev/esp32-service-uptime-monitor/dist/web.zip) for GitHub releases.
   - You can also manually package assets using:
     ```bash
     pio run -t packweb
     ```
   - Or deploy directly to an active device over Wi-Fi:
     ```bash
     pio run -t uploadweb
     ```
