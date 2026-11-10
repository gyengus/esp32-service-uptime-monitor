# ESP32 Service Uptime Monitor

An ESP32-S3 based monitoring device that periodically checks the availability of HTTP/HTTPS services and sends Discord notifications when status changes occur. It features a responsive Web UI, WebSocket live updates, OTA firmware and web asset flashing, and support for both MicroSD and internal Flash (LittleFS) storage.

## Features

- **Multi-board support**: Native configurations for `esp32-s3-cam` (with MicroSD slot) and `esp32-s3` (internal Flash with LittleFS) via `devices.h`
- **WiFi AP/STA mode**: Easy configuration via built-in captive portal access point or connection to existing WiFi
- **Dual storage backends**: Supports FAT32 MicroSD (1-bit SD_MMC) or internal SPI Flash with wear-leveling (LittleFS)
- **OTA Firmware updates**: Wireless updates via Web UI or PlatformIO (`esp32-s3-cam-ota` and `esp32-s3-ota`)
- **Over-the-Air Web UI updates**: Update web assets (.zip) over the network with automatic in-memory minification (`uploadweb`)
- **Modern Web UI**: Responsive Bootstrap 5 interface with dark mode, live status filtering, and entity management
- **WebSocket live stream**: Real-time log streaming and instant UI updates without polling
- **Discord notifications**: Rich embed notifications sent to a configured Discord webhook on status change
- **Config backup & restore**: Export and import configuration and monitored entity snapshots in JSON format
- **File Manager**: Built-in web file manager for browsing, uploading, downloading, and managing files
- **mDNS & SNTP**: Access via `http://<hostname>.local/` and automatic NTP time synchronization
- **LED indicators**: Visual feedback via addressable RGB LED (WS2812B) and optional status LED

## Hardware & Board Profiles

The project uses `include/devices.h` to define board pins and available hardware features (`HAS_SD_CARD`, `HAS_STATUS_LED`, `HAS_RGB_LED`) based on the active PlatformIO build environment:

| Feature / Pin | `esp32-s3-cam` (Default) | `esp32-s3` |
|---|---|---|
| **Target Board** | ESP32-S3 CAM Dev Board | ESP32-S3 DevKitC-1 / WROOM-1 |
| **Storage Backend** | MicroSD Card (`SD_MMC`, 1-bit) | Internal SPI Flash (`LittleFS`) |
| **Status LED** | GPIO 2 (`HAS_STATUS_LED`) | None |
| **RGB LED (WS2812B)** | GPIO 48 (`HAS_RGB_LED`) | GPIO 48 (`HAS_RGB_LED`) |
| **Boot Button** | GPIO 0 (`BOOT_BTN_PIN`) | GPIO 0 (`BOOT_BTN_PIN`) |
| **SD CMD / DATA / CLK** | GPIO 38 / 40 / 39 | N/A |

## Installation & Build

### Prerequisites

- [PlatformIO Core (CLI)](https://platformio.org/install/cli) or PlatformIO IDE
- Node.js & npm (for Web UI development and testing)
- MicroSD card (FAT32 formatted, only needed for `esp32-s3-cam`)

### Build & Upload Commands

#### Firmware Build & Upload

```bash
# Build default firmware (ESP32-S3 CAM with SD card support)
pio run -e esp32-s3-cam

# Build firmware for standard ESP32-S3 (internal Flash via LittleFS)
pio run -e esp32-s3

# Upload firmware via USB
pio run -e esp32-s3-cam --target upload
# or for standard S3:
pio run -e esp32-s3 --target upload

# Monitor serial output (115200 8N1)
pio device monitor
```

#### OTA Firmware Upload

Wirelessly update the firmware of an already deployed device:

```bash
# Upload to CAM board over OTA (default hostname: esp32_service_uptime_monitor.local)
pio run -e esp32-s3-cam-ota --target upload

# Upload to S3 board over OTA
pio run -e esp32-s3-ota --target upload

# Specify a custom IP address for OTA upload
ESP_IP=192.168.0.118 pio run -e esp32-s3-cam-ota --target upload
```

#### Web UI Asset Packaging & Upload

Web UI assets (HTML, JS, CSS, icons) reside in the storage filesystem (`/web`). You can build, minify, and upload them over the network without taking out the SD card or using serial:

```bash
# Package, minify in-memory, and upload directly to device storage
pio run --target uploadweb

# Upload to a custom IP or hostname
ESP_IP=192.168.0.118 pio run --target uploadweb

# Create the minified production archive locally without uploading (web.zip)
pio run --target packweb
```

### Initial Setup

1. **Power on the device**: If no configuration exists, the device boots into Access Point (AP) mode.
2. **Connect to AP**:
   - **SSID**: `ESP32-Service-Uptime-Monitor`
   - **Password**: `29965225`
   - **Captive Portal IP**: `192.168.4.1`
3. **Open browser**: Navigate to `http://192.168.4.1/`
4. **Configure Wi-Fi**: Enter your network credentials and click **Save**.
5. **Configure Discord** (optional): Add your Discord webhook URL to receive notifications.
6. **Device reboots**: Reconnects to your Wi-Fi network and starts monitoring.

## Usage

### Web Interface

Access the dashboard at `http://<hostname>.local/` or `http://<device-ip>/`.

- **Main Dashboard (`/`)**: Real-time status cards, filtering (ALL / ONLINE / OFFLINE), quick actions, footer system metrics (uptime, heap, RSSI, temperature).
- **Configuration Page (`/config`)**: Wi-Fi credentials, SNTP server, timezone offset, check interval, Discord webhook, LED brightness slider and live test, backup/restore, OTA firmware upload, and Web UI archive upload.
- **File Manager (`/fileManager`)**: Browse directories, upload files, download files, delete items, and create folders.

### Configuration Protection Toggle

Press the hardware Boot button (GPIO 0) at any time to toggle configuration access:
- **Enabled**: Settings button visible in the web header; `/config` and management endpoints unlocked.
- **Disabled**: Configuration protected; sensitive endpoints return 403 Forbidden.

## API Endpoints

### System & Health

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/health` | Health check (`{"status":"UP"}`) |
| `GET` | `/api/sysinfo` | System stats (heap, temperature, uptime, version, RSSI) |
| `GET` | `/api/summary` | Monitoring summary (total, online, offline counts) |
| `WS` | `/ws` | WebSocket connection for real-time state and log stream |

### Configuration

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/config` | Retrieve current configuration |
| `POST` | `/api/config` | Update configuration parameters |
| `GET` | `/api/config/backup` | Download complete configuration and entity backup JSON |
| `POST` | `/api/config/restore` | Restore configuration and entities from backup JSON |
| `POST` | `/api/config/factoryReset` | Reset settings to factory defaults |
| `POST` | `/api/config/test-led` | Preview RGB LED brightness (cyan pulse for 3 seconds) |
| `GET` | `/api/wifiscan` | Scan available Wi-Fi networks (AP mode only) |
| `POST` | `/api/testDiscord` | Send a test notification to the configured Discord webhook |

### Entity Management

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/monitoredentities` | List all monitored entities |
| `GET` | `/api/monitoredentity/{name}` | Retrieve details for a specific entity |
| `POST` | `/api/monitoredentity` | Add a new monitored entity |
| `PUT` | `/api/monitoredentity/{name}` | Update an existing entity |
| `DELETE` | `/api/monitoredentity/{name}` | Delete an entity |

### Updates & Storage

| Method | Endpoint | Description |
|---|---|---|
| `POST` | `/update` | Firmware binary upload handled by ElegantOTA |
| `POST` | `/api/web/upload-archive` | Upload and unpack `web.zip` archive into `/web` |
| `GET` | `/fileManager` | File Manager HTML interface |
| `GET` | `/list` | List files and directories in storage |
| `POST` | `/upload` | Upload a file to storage |
| `GET` | `/download` | Download a file from storage |
| `DELETE` | `/delete` | Delete a file or directory |
| `POST` | `/mkdir` | Create a new directory |

## Status Indicators

### RGB LED (GPIO 48)
- **Off**: All services online (steady state)
- **Pulsing Burgundy / Magenta**: One or more services are offline
- **Pulsing Amber**: Access Point (configuration) mode active
- **Pulsing Red**: System error (storage or critical fault)
- **Cyan**: LED brightness test active

### Status LED (GPIO 2, CAM board only)
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
  "hostname": "esp32_service_uptime_monitor",
  "logLevel": "DEBUG"
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

## Testing & Quality Assurance

```bash
# Run native C++ unit tests (Unity framework)
pio test -e native

# Run Web UI frontend unit tests (Vitest)
npm --prefix web test

# Run static analysis
pio check

# Clean build artifacts
pio run --target clean
```

## License

MIT

## Donations

If you find this project useful and would like to support its development:
- **Bitcoin**: `bc1qx4q5epl7nsyu9mum8edrvp2my8tut0enrz7kcn`
- **EVM**: `0x9F0a70A7306DF3fc072446cAF540F6766a4CC4E8`
