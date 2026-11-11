# ESP32 Service Uptime Monitor

An ESP32-based monitoring device that periodically checks the availability of HTTP/HTTPS services and sends Discord notifications when status changes occur. It features a responsive Web UI, WebSocket live updates, built-in Swagger/OpenAPI documentation, OTA firmware and web asset flashing, and support for both MicroSD and internal Flash (LittleFS) storage.

## Features

- **Multi-board support**: Native configurations for `esp32-s3-cam` (MicroSD slot), `esp32-s3` (LittleFS), `esp32` (classic dual-core ESP32-WROOM-32), `esp32-c3` (single-core RISC-V), `esp32-c6` (single-core RISC-V with Wi-Fi 6), and `esp32-c5` (single-core RISC-V with Dual-Band 2.4/5GHz Wi-Fi 6) via `devices.h`
- **WiFi AP/STA mode**: Easy configuration via built-in captive portal access point or connection to existing WiFi (supports 2.4 GHz and 5 GHz Dual-Band Wi-Fi 6)
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

# Monitor serial output (115200 8N1)
pio device monitor
```

> **Web Flasher (ESP Web Tools):** Pre-merged `dist/<env>.bin` images and `dist/manifest.json` can also be generated via `pio run -t dist` for one-click browser flashing via [ESP Web Tools](https://esphome.github.io/esp-web-tools/).

### Initial Setup

1. **Power on the device**: If no configuration exists, the device boots into Access Point (AP) mode. On first boot (or if `/web/index.html` is missing), the firmware automatically unpacks its embedded web UI archive into storage (`/web/`), so no manual file uploading is required.
2. **Connect to AP**:
   - **SSID**: `ESP32-Service-Uptime-Monitor`
   - **Password**: `29965225`
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
- **Configuration Page (`/config`)**: Wi-Fi credentials, SNTP server, timezone offset, check interval, Discord webhook, LED brightness slider and live test, backup/restore, OTA firmware upload, and Web UI archive upload.
- **Interactive API Documentation (`/api/docs` or `/swagger`)**: On-device Swagger UI backed by `/api/openapi.yaml` (also deployable statically to GitHub Pages from [`docs/api/`](docs/api/index.html)).
- **File Manager (`/fileManager`)**: Browse directories, upload files, download files, delete items, and create folders.

### Configuration Protection Toggle

Press the hardware **BOOT** button at any time to toggle configuration access:
- **Enabled**: Settings button visible in the web header; `/config` and management endpoints unlocked.
- **Disabled**: Configuration protected; sensitive endpoints return `403 Forbidden`.

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

*(Configuration endpoints require BOOT button unlock or AP mode; otherwise they return `403 Forbidden`.)*

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/config` | Retrieve current configuration |
| `POST` | `/api/config` | Update configuration parameters |
| `GET` | `/api/config/backup` | Download complete configuration and entity backup JSON |
| `POST` | `/api/config/restore` | Restore configuration and entities from backup JSON (`multipart/form-data`) |
| `POST` | `/api/config/factoryReset` | Reset settings to factory defaults |
| `POST` | `/api/config/test-led` | Preview RGB LED brightness (cyan pulse) |
| `GET` | `/api/wifiscan` | Trigger/poll non-blocking Wi-Fi network scan |
| `POST` | `/api/testDiscord` | Send a test notification to a Discord webhook URL |
| `POST` | `/api/web/upload-archive` | Upload and unpack `web.zip` archive into `/web` |
| `GET` | `/ota/start` | Initialize native firmware OTA session (`mode=fr`, optional `hash` MD5) |
| `POST` | `/ota/upload` | Firmware binary upload handled natively via `Update.h` |

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
  "ledBrightness": 50,
  "discordWebhookURL": "https://discord.com/api/webhooks/...",
  "hostname": "esp32-service-uptime-monitor",
  "logLevel": 8
}
```

### `/monitored_entities.json`
```json
[
  {
    "name": "Router",
    "address": "http://192.168.0.1/",
    "icon": "mdi-router-wireless",
    "notification": true,
    "disabled": false,
    "lastChanged": 1700000000,
    "failCount": 0
  }
]
```

## Development & Contributing

For multi-board builds, OTA targets, Web UI asset packing (`packweb` / `uploadweb`), OpenAPI doc generation flags (`--strict`, `--include-internal`), unit testing (`pio test -e native`, Vitest), and contribution guidelines, see **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## License

This project uses a **Dual License (Personal & Commercial)** — see [`LICENSE`](LICENSE) for the full terms:

- **Personal / Home Use (Free):** Individuals may use, compile, and modify the software free of charge for private, personal, non-commercial home/hobby monitoring, subject to keeping copyright notices intact.
- **Companies, Public Institutions & Commercial Use (Paid):** Any use by companies, organizations, public/government institutions, or for commercial products/services/hardware bundles requires a separate **Commercial License**. Please reach out via the project repository (<https://github.com/gyengus/esp32-service-uptime-monitor>) for commercial licensing inquiries.

## Donations

If you find this project useful and would like to support its development:
- **Bitcoin**: `bc1qx4q5epl7nsyu9mum8edrvp2my8tut0enrz7kcn`
- **EVM**: `0x9F0a70A7306DF3fc072446cAF540F6766a4CC4E8`
