# ESP32 Service Uptime Monitor

**Who watches your monitoring server when the server itself goes down?**

Turn any $4 ESP32 into a standalone, ultra-low-power (< 1 W) hardware uptime appliance. Periodically check HTTP/HTTPS services from an independent microcontroller, get instant **Discord webhook alerts** on status changes, monitor live telemetry over **WebSockets**, and see outages at a glance via **RGB LED indicators** — with zero cloud subscriptions and zero VM overhead.

[![Build](https://img.shields.io/github/actions/workflow/status/gyengus/esp32-service-uptime-monitor/pages.yml?style=for-the-badge&logo=githubactions&logoColor=white&label=Build)](https://github.com/gyengus/esp32-service-uptime-monitor/actions/workflows/pages.yml)
[![Release](https://img.shields.io/github/v/release/gyengus/esp32-service-uptime-monitor?style=for-the-badge&logo=github&label=Release)](https://github.com/gyengus/esp32-service-uptime-monitor/releases)
[![Web Flasher](https://img.shields.io/badge/Web_Flasher-Install_in_Browser-10b981?style=for-the-badge&logo=espressif&logoColor=white)](https://esp32-service-uptime-monitor.com/#flasher)
[![Live Preview & Docs](https://img.shields.io/badge/Live_UI_Preview-Documentation-3b82f6?style=for-the-badge)](https://esp32-service-uptime-monitor.com/)
[![OpenAPI 3.0](https://img.shields.io/badge/API-Swagger_UI-85ea2d?style=for-the-badge&logo=swagger&logoColor=black)](https://esp32-service-uptime-monitor.com/api/)
[![License](https://img.shields.io/badge/License-Free_for_Personal_%7C_30--Day_Business_Trial-f59e0b?style=for-the-badge)](LICENSE)

## Why a Dedicated Hardware Uptime Monitor?

- **Out-of-Band Independence:** Containerized monitors (like Uptime Kuma or Prometheus) go down with your hypervisor, NAS, or Docker host. An independent ESP32 keeps watching and alerts you even when your primary server crashes.
- **Flash in 60 Seconds from Your Browser:** No IDE or toolchain needed to get started. Connect via USB, click **Install** in the [Web Flasher](https://esp32-service-uptime-monitor.com/#flasher), and configure Wi-Fi directly in Chrome or Edge via **Improv Wi-Fi**.
- **Zero-Wear Steady State:** 3-retry hysteresis prevents false alarms and writes to flash/MicroSD **only when a service actually transitions** between `ONLINE` and `OFFLINE`.
- **Built for Homelabs, Ready for Business:** Free forever for personal homelabs and hobbyists, with a **30-day free evaluation trial** for companies. Equipped with a full REST API (OpenAPI 3 / Swagger UI), JSON backup/restore for rapid multi-device provisioning, and hardened security (physical BOOT button lock, SSRF & DNS rebinding protection) for commercial and MSP deployments.

## Features

- **Multi-board support**: Native configurations for `esp32-s3-cam` (MicroSD slot), `esp32-s3` (LittleFS), `esp32` (classic dual-core ESP32-WROOM-32), `esp32-c3` (single-core RISC-V), `esp32-c6` (single-core RISC-V with Wi-Fi 6), and `esp32-c5` (single-core RISC-V with Dual-Band 2.4/5GHz Wi-Fi 6) via `devices.h`
- **WiFi AP/STA & Improv Serial mode**: Easy configuration via browser-based **Improv Wi-Fi** (over USB Serial in ESP Web Tools), built-in captive portal access point, or connection to existing WiFi (supports 2.4 GHz and 5 GHz Dual-Band Wi-Fi 6)
- **Dual storage backends**: Supports FAT32 MicroSD (1-bit SD_MMC) or internal SPI Flash with wear-leveling (LittleFS)
- **OTA Firmware updates**: Wireless updates via Web UI or PlatformIO (`*-ota` environments)
- **Modern Web UI**: Responsive Bootstrap 5 interface with dark mode, live status filtering, and entity management
- **Interactive API Docs (Swagger UI)**: Available directly on the device at `/api/docs` (or `/swagger`) and for static hosting in [`docs/api/`](docs/api/index.html)
- **WebSocket live stream**: Real-time log streaming and instant UI updates without polling
- **Discord notifications**: Rich embed notifications sent to a configured Discord webhook on status change
- **Config backup & restore**: Export and import configuration and monitored entity snapshots in JSON format
- **File Manager**: Built-in web file manager for browsing, uploading, downloading, and managing files during development
- **mDNS & SNTP**: Access via `http://<hostname>.local/` and automatic NTP time synchronization
- **LED indicators**: Visual feedback via addressable RGB LED (WS2812B) and optional status LED

## Hardware & Board Profiles

The project uses [`include/devices.h`](include/devices.h) to define board pins and available hardware features (`HAS_SD_CARD`, `HAS_STATUS_LED`, `HAS_RGB_LED`) based on the active PlatformIO build environment:

| Feature / Pin | `esp32-s3-cam` | `esp32-s3` | `esp32` | `esp32-c3` | `esp32-c6` | `esp32-c5` |
|---|---|---|---|---|---|---|
| **Target Board** | ESP32-S3 CAM Dev Board | ESP32-S3 DevKitC-1 / WROOM-1 | ESP32-WROOM-32 / NodeMCU-32S | ESP32-C3 SuperMini / DevKitM-1 | ESP32-C6 DevKitM-1 / DevKitC-1 | ESP32-C5 DevKitC-1 |
| **Architecture** | Dual-core Xtensa LX7 | Dual-core Xtensa LX7 | Dual-core Xtensa LX6 | Single-core RISC-V | Single-core RISC-V (Wi-Fi 6) | Single-core RISC-V (2.4/5GHz Wi-Fi 6) |
| **Storage Backend** | MicroSD Card (`SD_MMC`, 1-bit) | Internal Flash (`LittleFS`, 16MB) | Internal Flash (`LittleFS`, 4MB) | Internal Flash (`LittleFS`, 4MB) | Internal Flash (`LittleFS`, 4MB) | Internal Flash (`LittleFS`, 4MB) |
| **Status LED** | GPIO 2 (`HAS_STATUS_LED`) | None | GPIO 2 (`HAS_STATUS_LED`) | GPIO 8 (`HAS_STATUS_LED`) | None | None |
| **RGB LED (WS2812B)** | GPIO 48 (`HAS_RGB_LED`) | GPIO 48 (`HAS_RGB_LED`) | None | None | GPIO 8 (`HAS_RGB_LED`) | GPIO 27 (`HAS_RGB_LED`) |
| **Boot Button** | GPIO 0 (`BOOT_BTN_PIN`) | GPIO 0 (`BOOT_BTN_PIN`) | GPIO 0 (`BOOT_BTN_PIN`) | GPIO 9 (`BOOT_BTN_PIN`) | GPIO 9 (`BOOT_BTN_PIN`) | GPIO 28 (`BOOT_BTN_PIN`) |
| **SD CMD / DATA / CLK** | GPIO 38 / 40 / 39 | N/A | N/A | N/A | N/A | N/A |

## Quick Start & Installation

### Flash via USB (PlatformIO)

```bash
# Build and flash firmware for your board via USB
pio run -e esp32-s3-cam --target upload
# Or: esp32-s3 | esp32 | esp32-c3 | esp32-c6 | esp32-c5

# Production release build (disables File Manager & memory stats logging, sets log level to INFO):
# RELEASE=1 pio run -e esp32-s3-cam --target upload

# Monitor serial output (115200 8N1)
pio device monitor
```

> **Web Flasher & Improv Wi-Fi (ESP Web Tools):** Pre-merged `dist/<env>.bin` images and `dist/manifest.json` can also be generated via `pio run -t dist` for one-click browser flashing and serial Wi-Fi provisioning via [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and [Improv Wi-Fi](https://www.improv-wifi.com/).

### Initial Setup

#### Option A: Browser Provisioning via Improv Wi-Fi (USB Serial)
1. Flash the firmware from the Web Flasher (or connect an already flashed board via **Connect** in ESP Web Tools).
2. Once the device boots, select **Connect to Wi-Fi** in the browser dialog, choose your Wi-Fi network from the scanned list, and enter the password.
3. The device connects to your network, saves `/config.json`, returns a direct **Visit Device** link (`http://<hostname>.local/`), and reboots into normal monitoring mode.

#### Option B: Captive Portal Access Point
1. **Power on the device**: If no configuration exists, the device boots into Access Point (AP) mode. On first boot (or if `/web/index.html` is missing), the firmware automatically unpacks its embedded web UI archive into storage (`/web/`), so no manual file uploading is required.
2. **Connect to AP**:
   - **SSID**: `ESP32-Service-Uptime-Monitor`
   - **Security**: Open (no password required for initial setup)
   - **Captive Portal IP**: `192.168.4.1`
3. **Open browser**: Navigate to `http://192.168.4.1/` (most devices trigger the captive portal automatically).
4. **Configure Wi-Fi**: Enter your network credentials and click **Save**.
5. **Configure Discord** (optional): Add your Discord webhook URL to receive notifications.
6. **Device reboots**: Reconnects to your Wi-Fi network and starts monitoring. The web dashboard will be available at `http://esp32-service-uptime-monitor.local/` (or your configured hostname) or via its assigned DHCP IP address.

## Usage

### Web Interface

Access the dashboard at `http://<hostname>.local/` (default: `http://esp32-service-uptime-monitor.local/`) or `http://<device-ip>/`.

> [!TIP]
> **Hostname & mDNS Compatibility:** In accordance with RFC 1123, device hostnames only allow lowercase ASCII letters (`a-z`), digits (`0-9`), and hyphens (`-`). Any spaces, dots, or underscores entered via the web interface are automatically sanitized to hyphens to ensure seamless mDNS resolution across all operating systems, including Windows, macOS, iOS, Android, and Linux.

- **Main Dashboard (`/`)**: Real-time status cards, filtering (ALL / ONLINE / OFFLINE), quick actions, footer system metrics (uptime, heap, RSSI, temperature) and device log terminal.
- **Configuration Page (`/config`)**: Wi-Fi credentials, SNTP server, timezone offset, check interval, Discord webhook, OTA update token, LED brightness slider and live test, backup/restore, OTA firmware upload, and Web UI archive upload.
- **Interactive API Documentation (`/api/docs` or `/swagger`)**: On-device Swagger UI backed by `/api/openapi.yaml` (also deployable statically to GitHub Pages from [`docs/api/`](docs/api/index.html)).
- **File Manager (`/fileManager`)**: Browse directories, upload files, download files, delete items, and create folders. Can be completely disabled in production releases from the command line (`DISABLE_FILE_MANAGER=1 pio run` or `RELEASE=1 pio run`), via `#define DISABLE_FILE_MANAGER` in `include/Config.h`, or `-D DISABLE_FILE_MANAGER` in `platformio.ini`, omitting `fileManager.html` from storage and saving ~15 KB flash.

### Configuration Protection & Factory Reset

The hardware **BOOT** button controls configuration mode and factory reset:
- **Short Press (< 5s)**: Toggles configuration access. When enabled, the Settings icon and a real-time countdown badge (`10:00`) appear in the web header/menu, and `/config`, entity editing, and management routes are unlocked. For security, configuration mode automatically times out after **10 minutes of inactivity** (each interaction resets the timer).
- **Long Press (≥ 10s)**: Initiates an immediate **Factory Reset** — deletes stored configurations and entities (`config.json`, `monitored_entities.json`) and restarts the device into Open Setup AP mode.

## API Overview

Full interactive request/response schemas and live testing are available in the Swagger UI at `http://<device-address>/api/docs` and in [`docs/api/openapi.yaml`](docs/api/openapi.yaml).

### System & Monitoring

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/health` | Health check (`{"status":"UP"}`) |
| `GET` | `/api/sysinfo` | Basic system stats (heap, PSRAM, temperature, uptime, version, RSSI) |
| `GET` | `/api/sysinfo/details` | Detailed device, memory, storage, network, monitoring, and firmware diagnostics |
| `GET` | `/api/summary` | Monitoring summary (total, online, offline counts) |
| `GET` | `/api/monitoredentities` | List all monitored entities |
| `GET` | `/api/monitoredentity/{name}` | Retrieve details for a specific entity |
| `POST` | `/api/monitoredentity` | Add a new monitored entity |
| `PUT` | `/api/monitoredentity/{name}` | Update an existing entity |
| `DELETE` | `/api/monitoredentity/{name}` | Delete an entity |
| `WS` | `/ws` | WebSocket connection for real-time state updates and live log stream |

### Configuration & Updates

*(Configuration endpoints require BOOT button unlock or AP mode; otherwise they return `403 Forbidden`. Alternatively, OTA and web archive endpoints accept an `X-PlatformIO-Upload` HTTP header matching the configured `otaToken`.)*

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/config` | Retrieve current configuration (sensitive secrets masked as `******`) |
| `POST` | `/api/config` | Update configuration parameters |
| `GET` | `/api/config/backup` | Download complete configuration and entity backup JSON (`Cache-Control: no-store`; contains Wi-Fi and webhook secrets) |
| `POST` | `/api/config/restore` | Restore configuration and entities from backup JSON (`multipart/form-data`) |
| `POST` | `/api/config/factoryReset` | Reset settings to factory defaults |
| `POST` | `/api/config/test-led` | Preview RGB LED brightness (cyan pulse) |
| `GET` | `/api/wifiscan` | Trigger/poll non-blocking Wi-Fi network scan |
| `POST` | `/api/testDiscord` | Send a test notification to a Discord webhook URL |
| `POST` | `/api/web/upload-archive` | Upload and unpack `web.zip` archive into `/web` |
| `GET` | `/ota/start` | Initialize native firmware OTA session (`mode=fr`, optional `hash` MD5) |
| `POST` | `/ota/upload` | Firmware binary upload handled natively via `Update.h` |

### Security Considerations

- **CSRF & DNS Rebinding Protection**: State-mutating endpoints enforce `Content-Type: application/json` and validate the `Host` (DNS rebinding protection against foreign domains) and `Origin`/`Referer` headers.
- **Physical BOOT Button Authorization**: Management endpoints and log level alterations require physical activation via the BOOT button (10-minute auto-expiry with visual countdown).
- **SSRF & Address Validation**: Monitored addresses and webhook URLs are strictly validated to HTTP/HTTPS schemes; local loopback (`127.0.0.0/8`, `::1`), link-local, and broadcast IP targets are blocked to prevent Server-Side Request Forgery.
- **WebSocket & Log Hygiene**: Live log streaming (`/ws`) is restricted to trusted local origins (maximum 3 concurrent log subscribers). In production release builds (`RELEASE=1`), `DEBUG`/`TRACE`/`VERBOSE` logs and runtime log-level elevation above `INFO` are disabled. Monitored query parameters (`?token=...`) and fragments are stripped from HTTP check log messages, and format strings are strictly enforced.
- **OTA Hardening**: OTA firmware and web archive uploads are disabled in open AP setup mode and protected against brute-force token guessing via constant-time comparison and automatic 60-second lockout after 5 consecutive failures.
- **Secret Masking**: Passwords, OTA tokens, and Discord webhook authorization tokens are masked as `******` when retrieving device configuration via `GET /api/config`.


## Status Indicators

### RGB LED (`HAS_RGB_LED`)
- **Off**: All services online (steady state)
- **Pulsing Burgundy / Magenta**: One or more services are offline
- **Pulsing Amber**: Access Point (configuration) mode active
- **Pulsing Red**: System error (storage or critical fault)
- **Cyan**: LED brightness test active

### Status LED (`HAS_STATUS_LED`)
- **Blink**: Active network check cycle executing

## Configuration Files

The device stores its persistent state in JSON format in the root of the filesystem:

### `/config.json`
```json
{
  "wifiSSID": "YourNetworkSSID",
  "wifiPassword": "YourPassword",
  "sntpServer": "pool.ntp.org",
  "offsetHours": 1,
  "checkInterval": 60,
  "discordWebhookURL": "https://discord.com/api/webhooks/...",
  "hostname": "esp32-service-uptime-monitor",
  "logLevel": 8,
  "logLevelName": "DEBUG",
  "ledBrightness": 50,
  "otaToken": "your-ota-token"
}
```

### `/monitored_entities.json`
```json
[
  {
    "name": "Router",
    "address": "http://192.168.0.1/",
    "icon": "mdi-router-wireless",
    "isOnline": true,
    "notification": true,
    "disabled": false,
    "lastChanged": 1700000000
  }
]
```

## Development & Contributing

For multi-board builds, OTA targets, Web UI asset packing (`packweb` / `uploadweb`), OpenAPI doc generation flags (`--strict`, `--include-internal`), unit testing (`pio test -e native`, Vitest), and contribution guidelines, see **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## License

This project uses a **Dual License (Personal & Commercial)** — see [`LICENSE`](LICENSE) for the full terms:

- **Personal / Home Use (Free):** Individuals may use, compile, and modify the software free of charge for private, personal, non-commercial home/hobby monitoring, subject to keeping copyright notices intact.
- **Companies, Public Institutions & Commercial Use (30-Day Free Trial, then Paid):** Organizations may internally evaluate and test the software **free of charge for up to 30 days**. Continued production use, MSP deployments, or commercial hardware bundles require a separate **Commercial License**. Please reach out via the [contact form](https://gyengus.hu/kapcsolat?utm_source=uptimeMonitorReadme&utm_medium=referral&utm_campaign=commercial_license) or the project repository (<https://github.com/gyengus/esp32-service-uptime-monitor>) for commercial licensing inquiries.

## Donations

If you find this project useful and would like to support its development:
- **Bitcoin**: `bc1qx4q5epl7nsyu9mum8edrvp2my8tut0enrz7kcn`
- **EVM**: `0x9F0a70A7306DF3fc072446cAF540F6766a4CC4E8`
