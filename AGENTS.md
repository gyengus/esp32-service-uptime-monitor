# AGENTS.md

This file provides guidance to AI agents when working with code in this repository.

## Build & Deploy

```bash
pio run                                           # Build default firmware (esp32-s3-cam)
pio run -e esp32-s3                               # Build firmware without SD (LittleFS)
pio run --target upload                           # Upload firmware via USB
pio run -e esp32-s3-cam-ota --target upload       # Upload CAM firmware via OTA (ESP_IP=x.x.x.x)
pio run -e esp32-s3-ota --target upload           # Upload S3 firmware via OTA (ESP_IP=x.x.x.x)
pio run --target packweb                          # Pack & minify Web UI into web.zip
pio run --target uploadweb                        # Pack, minify & upload Web UI to device storage
pio device monitor                                # Serial monitor (115200 8N1)
pio run --target clean                            # Clean build artifacts
pio check                                         # Lint (cppcheck)
pio test -e native                                # Run native unit tests
```

Build flags: `MIN_LOG_LEVEL=ELOG_LEVEL_DEBUG`, PSRAM enable, `CONFIG_ASYNC_TCP_RUNNING_CORE=1`, `ELEGANTOTA_USE_ASYNC_WEBSERVER=1`
Partition table: `partitions_16MB.csv` (dual 6.5MB OTA app partitions `app0`/`app1`, NVS, otadata, coredump)

## Architecture

**Singleton services & helpers** (getInstance() + deleted copy constructor where applicable):

| Service / Module | Responsibility |
|------------------|----------------|
| `MonitoringService` | HEAD/PING checks, Discord notification after 3 failures |
| `NotificationService` | HTTP POST to Discord webhook (WiFiClientSecure) |
| `NetworkContext` | Wi-Fi management (STA/AP), connection, SSID persistence |
| `Config` | Load/save `/config.json` from SD |
| `MonitoredEntityRepository` | Thread-safe entity management (semaphore), `/monitored_entities.json` |
| `Storage` | Storage interface (SD_MMC or LittleFS based on profile) |
| `WebArchiveExtractor` | Extracts uploaded Web UI ZIP archive directly onto storage with PSRAM buffers |
| `ElegantOTA` | Web/HTTP firmware update handler integrated with AsyncWebServer |

**Globals** (`globals.h`): `isConfigFunctionsEnabled`, `isAPMode`, `shouldRestart`, `MonitoringTaskHandle`, `temp_sensor`

## Processes

**Boot sequence**:
1. SD initialization → `storage.init()`
2. Cancel OTA rollback if booted fine → `esp_ota_mark_app_valid_cancel_rollback()`
3. `/config.json` exists? → Yes: `initNormalMode()`, No: `configurationMode()` (AP)
4. Normal mode: Wi-Fi connect → mDNS (http://hostname.local) → SNTP time sync → `setupOTA()` → `MonitoringService::init()`
5. `xTaskCreatePinnedToCore(monitoringTaskLoop, ..., 0)` - runs on Core 0 (WiFi on Core 1)

**Monitoring task** (Core 0, 5 sec initial delay):
- Periodic `checkAll()` based on config interval
- HTTP HEAD request check for each entity
- Status change to OFFLINE after 3 consecutive failures
- Discord notification on status change (if `notification=true`)
- Auto-save via `MonitoredEntityRepository::save()`

**OTA & Web UI updates**:
- ElegantOTA handles firmware `.bin` uploads on `/update`. On OTA start: monitoring task is suspended and SD card is unmounted to prevent bus contention or SPI collisions.
- Web UI update accepts `.zip` at `/api/web/upload-archive`. Extracted safely to SD card using `WebArchiveExtractor` (`unzipLIB`).
- PlatformIO script `scripts/pio_ota_targets.py` provides `packweb` and `uploadweb` with in-memory minification (HTML/CSS/JS) before ZIP creation without touching source files on disk.

**Web UI**:
- `/` - main page (index.html), `/config` - settings page (includes OTA firmware & web asset update forms)
- `/update` - ElegantOTA firmware update endpoint
- `/api/web/upload-archive` - Web UI archive upload endpoint
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
