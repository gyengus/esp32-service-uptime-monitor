# AGENTS.md

This file provides guidance to AI agents when working with code in this repository.

## Build & Deploy

```bash
pio run              # Build
pio run --target upload   # Upload firmware
pio device monitor        # Serial monitor (115200 8N1)
pio run --target clean    # Clean build artifacts
pio check                 # Lint (cppcheck)
```

Build flags: `MIN_LOG_LEVEL=ELOG_LEVEL_DEBUG`, PSRAM enable, `CONFIG_ASYNC_TCP_RUNNING_CORE=1`

## Architecture

**Singleton services** (getInstance() + deleted copy constructor):

| Service | Responsibility |
|---------|----------------|
| `MonitoringService` | HEAD/PING checks, Discord notification after 3 failures |
| `NotificationService` | HTTP POST to Discord webhook (WiFiClientSecure) |
| `NetworkContext` | Wi-Fi management (STA/AP), connection, SSID persistence |
| `Config` | Load/save `/config.json` from SD |
| `MonitoredEntityRepository` | Thread-safe entity management (semaphore), `/monitored_entities.json` |
| `Storage` | SD_MMC interface (read/write/delete) |

**Globals** (`globals.h`): `isConfigFunctionsEnabled`, `isAPMode`, `shouldRestart`, `MonitoringTaskHandle`, `temp_sensor`

## Processes

**Boot sequence**:
1. SD initialization → `storage.init()`
2. `/config.json` exists? → Yes: `initNormalMode()`, No: `configurationMode()` (AP)
3. Normal mode: Wi-Fi connect → mDNS (http://hostname.local) → SNTP time sync → `MonitoringService::init()`
4. `xTaskCreatePinnedToCore(monitoringTaskLoop, ..., 0)` - runs on Core 0 (WiFi on Core 1)

**Monitoring task** (Core 0, 5 sec initial delay):
- Periodic `checkAll()` based on config interval
- HTTP HEAD request check for each entity
- Status change to OFFLINE after 3 consecutive failures
- Discord notification on status change (if `notification=true`)
- Auto-save via `MonitoredEntityRepository::save()`

**Web UI**:
- `/` - main page (index.html), `/config` - settings page
- `/api/wifiscan` - scan networks (in AP mode)
- `/api/config` - load/save configuration
- `/api/monitoredentities` - list entities (GET)
- `/api/monitoredentity` - create (POST), update (PUT), delete (DELETE)
- `/api/summary` - summary (total/online/offline)
- `/api/sysinfo` - system info (heap, rssi, uptime, temperature, version)

**Configuration mode (AP)**:
- SSID: `ESP32-Service-Uptime-Monitor`, password: none
- IP: `192.168.4.1`, captive portal
- Boot button (`BOOT_BTN_PIN=0`) toggles `isConfigFunctionsEnabled`

## MonitoredEntity

```cpp
struct {
  String name;           // identifier (hostname-like)
  String address;        // HTTP(S) URL or address
  String icon;           // UI icon
  bool notification;     // enable notifications
  bool disabled;         // disabled flag
  time_t lastChanged;    // last status change timestamp
  uint8_t failCount;     // failure counter (reset on success)
}
```

## Key Settings

| Setting | Value |
|---------|-------|
| Board | `esp32-s3-devkitc-1` |
| PSRAM | `opi` (8MB), `qio_opi` memory type |
| Stack | `8192` bytes (for HTTPS requests) |
| Task priority | `5` (webserver on Core 1, monitoring on Core 0) |
| HTTP timeout | `5000` ms |
| Retry | `3` attempts |
| Check interval | configurable (default: 60s) |
| SNTP sync | every 15 minutes |

## Development Notes

- **Hot-reload**: Full rebuild & upload required
- **Logging**: `Logger.info(WARNING/ERROR/DEBUG)(MAINLOG, format, ...)`
- **Thread safety**: `xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY)`
- **PSRAM**: Use `PSRAMAllocator` for `JsonDocument`
- **Hostname**: `cleanHostname()` - alphanumeric, `_`, `-` only

## Compact instructions

When you are using compact, please focus onn code changes.
