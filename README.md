# ESP32 Service Uptime Monitor

An ESP32-S3 based monitoring device that periodically checks the availability of services (via HTTP HEAD or ICMP PING) and sends Slack notifications when status changes occur.

## Features

- **Dual monitoring types**: HTTP HEAD requests or ICMP PING
- **WiFi AP/STA mode**: Easy configuration via built-in access point or connect to existing WiFi
- **Web UI**: Bootstrap-based interface for monitoring and configuration
- **Slack integration**: Send notifications to Slack channels on status changes
- **SD card storage**: Persistent configuration and entity storage
- **mDNS support**: Access via hostname.local
- **SNTP time sync**: Automatic time synchronization
- **LED status indicator**: Visual feedback on monitoring status
- **Temperature monitoring**: ESP32 internal temperature monitoring

## Hardware

- **Microcontroller**: ESP32-S3 (e.g., `esp32-s3-devkitc-1`)
- **Storage**: MicroSD card (SPI mode, 1-bit, up to 8GB recommended)
- **LED**: RGB LED on GPIO 48, status LED on GPIO 2
- **Button**: Boot button (GPIO 0) for configuration mode toggle

### Pinout

| Pin | Function |
|-----|----------|
| GPIO 2 | Status LED |
| GPIO 48 | RGB LED |
| GPIO 0 | Boot button (configuration toggle) |
| GPIO 38 | SD Card CMD |
| GPIO 40 | SD Card DATA |
| GPIO 39 | SD Card CLK |

## Installation

### Prerequisites

- PlatformIO CLI installed
- MicroSD card (formatted as FAT32)

### Build & Upload

```bash
# Build firmware
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

### Initial Setup

1. **Power on the device** - The device will boot into configuration mode (AP) if no config file is found
2. **Connect to AP**: SSID `ESP32-Service-Uptime-Monitor`, password `29965225`
3. **Open browser**: Navigate to `http://192.168.4.1/`
4. **Configure WiFi**: Enter your network credentials and click Save
5. **Configure Slack** (optional): Enter your Slack webhook URL to enable notifications
6. **Device restarts** and connects to your WiFi

## Usage

### Web Interface

Access the device at `http://<hostname>.local/` or `http://<IP>/`

#### Main Page (`/`)

- View all monitored entities with status (online/offline)
- Filter by status (ALL/ONLINE/OFFLINE)
- Add new entities
- Edit or delete existing entities
- Real-time system stats in footer (uptime, RSSI, heap, temperature)

#### Configuration Page (`/config`)

- WiFi SSID and password
- SNTP server (default: `pool.ntp.org`)
- Timezone offset (hours)
- Check interval (seconds, default: 60)
- Slack webhook URL
- Device hostname

### Adding a Monitored Entity

1. Click the `+` button (FAB - Floating Action Button)
2. Fill in:
   - **Name**: Unique identifier for the entity
   - **Address**: IP address, hostname, or URL to monitor
   - **Type**: HTTP HEAD or ICMP PING
   - **Notifications**: Enable Slack notifications on status change
   - **Disabled**: Skip this entity from monitoring
   - **Icon**: Material Design Icons class (default: `mdi-server`)
3. Click Save

### Configuration Toggle

Press the boot button to toggle configuration mode:
- **Enabled**: Configuration UI accessible, config button visible in main UI
- **Disabled**: Configuration UI protected, config button hidden

## API Endpoints

### System

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/health` | Health check (returns `{"status":"UP"}`) |
| GET | `/api/sysinfo` | System info (heap, temperature, version, commit hash) |
| GET | `/api/summary` | Entity summary (total, online, offline counts) |

### Configuration

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/config` | Get current configuration |
| POST | `/api/config` | Update configuration |
| POST | `/api/config/factoryReset` | Reset to factory defaults |
| GET | `/api/wifiscan` | Scan for WiFi networks (AP mode only) |
| POST | `/api/testSlack` | Test Slack webhook integration |

### Entities

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/monitoredentities` | List all monitored entities |
| GET | `/api/monitoredentity/{name}` | Get entity details |
| POST | `/api/monitoredentity` | Create new entity |
| PUT | `/api/monitoredentity/{name}` | Update entity |
| DELETE | `/api/monitoredentity/{name}` | Delete entity |

## Configuration File

### `/config.json`

```json
{
  "wifiSSID": "YourNetworkSSID",
  "wifiPassword": "YourPassword",
  "sntpServer": "pool.ntp.org",
  "offsetHours": 1,
  "checkInterval": 60,
  "slackWebhookURL": "https://hooks.slack.com/services/...",
  "hostname": "esp32_service_uptime_monitor"
}
```

### `/monitored_entities.json`

```json
[
  {
    "name": "WebServer",
    "address": "https://example.com",
    "type": "HEAD",
    "icon": "mdi-web",
    "notification": true,
    "disabled": false,
    "lastChanged": 1700000000
  }
]
```

## Status Indicators

- **LED (GPIO 2)**: Blinking during monitoring cycle
- **RGB LED (GPIO 48)**:
  - Normal mode: Solid green (online), solid red (offline)
  - Error mode: Pulsing red (SD card or configuration error)
- **AP Mode**: LED pulsing in amber color

## Troubleshooting

| Issue | Solution |
|-------|----------|
| No WiFi connection | Check SSID/password, verify AP mode first |
| Config not saved | Check SD card is properly inserted and writable |
| Slack notifications not working | Test webhook URL via `/config` page |
| Device not connecting to WiFi | Check hostname format (alphanumeric, `_`, `-` only) |

## Development

```bash
# Clean build
pio run --target clean

# Linting
pio check

# Upload OTA (if supported)
pio run --target upload
```

## License

MIT
