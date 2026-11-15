# Contributing & Development Guide

Thank you for contributing to **ESP32 Service Uptime Monitor**. This guide covers the local development setup, build targets, Web UI packaging, OpenAPI/Swagger documentation generation, and testing workflows.

> For architectural conventions and AI agent coding rules, also see **[AGENTS.md](AGENTS.md)**.

---

## Prerequisites

- [PlatformIO Core (CLI)](https://platformio.org/install/cli) or PlatformIO IDE
- **Python 3** with **PyYAML** (`pip install pyyaml`) — required for OpenAPI specification generation (`PyYAML` is installed in the PlatformIO Python environment when running `pio run`)
- **Node.js & npm** — required for Web UI unit tests and `terser` JS minification (`npm --prefix web install`)

---

## Building Firmware

### Standard Builds

```bash
# Build all default board environments
pio run

# Build for a specific target board
pio run -e esp32-s3-cam   # ESP32-S3-CAM (MicroSD)
pio run -e esp32-s3       # ESP32-S3 DevKit (LittleFS, 16MB)
pio run -e esp32          # Classic ESP32-WROOM-32 (LittleFS, 4MB)
pio run -e esp32-c3       # ESP32-C3 SuperMini (LittleFS, 4MB)
pio run -e esp32-c6       # ESP32-C6 DevKit (LittleFS, 4MB)
pio run -e esp32-c5       # ESP32-C5 DevKit (LittleFS, 4MB)

# Build with File Manager excluded for release (omits fileManager.html, saves ~15 KB flash)
DISABLE_FILE_MANAGER=1 pio run

# Build with periodic memory statistics logging excluded
DISABLE_MEM_STATS_LOGGING=1 pio run

# Build release versions (disables File Manager and memory stats logging, sets log level to INFO)
RELEASE=1 pio run

# Flash via USB and monitor serial output (115200 8N1)
pio run -e esp32-s3-cam --target upload
pio device monitor

# Clean build artifacts
pio run --target clean
```

### Release Binaries & ESP Web Tools Manifest

```bash
# Export standalone OTA binaries, merged factory-*.bin images, web.zip, and manifest.json to dist/
pio run -t dist
```

### Wireless OTA Firmware Upload

```bash
# Upload over OTA (default target: esp32-service-uptime-monitor.local)
pio run -e esp32-s3-cam-ota --target upload
pio run -e esp32-s3-ota --target upload
pio run -e esp32-ota --target upload
pio run -e esp32-c3-ota --target upload
pio run -e esp32-c6-ota --target upload
pio run -e esp32-c5-ota --target upload

# Override target IP or mDNS hostname
ESP_IP=192.168.0.118 pio run -e esp32-s3-cam-ota --target upload
ESP_HOST=my-device.local pio run -e esp32-s3-cam-ota --target upload
pio run -e esp32-s3-ota --target upload --upload-port 192.168.0.118
```

**OTA Target Resolution Order:**
1. `ESP_HOST` / `ESP_IP` environment variables
2. `upload_port` in `platformio.ini` (or `--upload-port` CLI flag)
3. `hostname` in local `config.json` (if present)
4. Fallback: `esp32-service-uptime-monitor.local`

---

## Web UI Asset Pipeline

Web UI sources live in `web/` and are minified and packed into `web.zip` and `src/EmbeddedWebArchive.cpp` (PROGMEM) automatically before firmware compilation (`pre:scripts/generate_web_embed.py`). The archive also embeds `docs/api/index.html` (`api-docs.html`), `docs/api/openapi.yaml` (`openapi.yaml`), and `LICENSE`.

```bash
# Package, minify, and upload Web UI directly to a running device over HTTP
pio run --target uploadweb

# Upload to a custom device IP or hostname
ESP_IP=192.168.0.118 pio run --target uploadweb
ESP_HOST=my-device.local pio run --target uploadweb

# Generate web.zip locally without uploading
pio run --target packweb
```

---

## API Documentation (OpenAPI & Swagger UI)

The REST API specification is generated directly from `@openapi` JSDoc-style YAML blocks inside the C++ source files (`include/Api.h`, `src/Api.cpp`, `lib/FileManager/src/FileManager.cpp`).

### How It Works

- Top-level metadata, `servers` variables, `tags`, and reusable `components.schemas` live in [`include/Api.h`](include/Api.h).
- Each endpoint's OpenAPI block sits directly above its `server.on(...)` registration in [`src/Api.cpp`](src/Api.cpp) or [`lib/FileManager/src/FileManager.cpp`](lib/FileManager/src/FileManager.cpp).
- `info.version` is automatically populated from `FIRMWARE_VERSION` in [`include/version.h`](include/version.h).
- Endpoints marked with `x-internal: true` (such as the File Manager endpoints) are excluded from the public `docs/api/openapi.yaml` by default, along with any orphaned tags and schemas.
- During `pio run`, `pre:scripts/generate_openapi.py` runs **before** `pre:scripts/generate_web_embed.py` so the updated `docs/api/openapi.yaml` is always embedded into the firmware as well.

### Generator CLI & Flags

```bash
# Generate docs/api/openapi.yaml (excludes x-internal: true endpoints, warns on missing routes)
python3 scripts/openapi_generator.py

# Fail with exit code 1 if any server.on(...) route is missing an @openapi block (recommended for CI)
python3 scripts/openapi_generator.py --strict

# Include internal/dev-only endpoints (e.g. File Manager) in the generated YAML
python3 scripts/openapi_generator.py --include-internal

# Custom output path or project root
python3 scripts/openapi_generator.py --output docs/api/openapi-dev.yaml --include-internal
python3 scripts/openapi_generator.py --project-dir /path/to/repo
```

### Local Swagger UI Preview

```bash
python3 -m http.server 8000 --directory docs/api
```

Open `http://localhost:8000/` in your browser and enter your ESP32 IP or mDNS hostname in the **Servers → `host`** field.

### Local GitHub Pages Site Preview

To preview the full static GitHub Pages site (`docs/index.html`, `docs/docs.html`, and `docs/api/index.html`) with local `README.md`, `CONTRIBUTING.md`, and `LICENSE` resolution, start a local HTTP server from the repository root:

```bash
python3 -m http.server 8765
```

Then open in your browser:
- **Overview & Web Flasher:** `http://localhost:8765/docs/index.html`
- **Documentation Reader:** `http://localhost:8765/docs/docs.html`
- **Swagger API Docs:** `http://localhost:8765/docs/api/index.html`

---

## Testing & Quality Assurance

```bash
# Run all native C++ unit tests (Unity + ArduinoFake)
pio test -e native

# Run native tests and generate code coverage report (Custom task: "Generate Coverage Report")
pio run -t coverage

# Run a single C++ test suite
pio test -e native -f test_api

# Run Web UI frontend unit tests (Vitest)
npm --prefix web test

# Run Web UI tests with coverage
npm --prefix web run test:coverage

# Run PlatformIO static analysis
pio check
```

---

## Coding & Commit Guidelines

- Follow all architectural and thread-safety rules in [AGENTS.md](AGENTS.md).
- Keep device hostnames RFC 1123 compliant (`a-z`, `0-9`, `-` only; sanitize user input via `HostnameUtils::cleanHostname()`).
- Write clear, descriptive commit messages in English **without** Conventional Commits prefixes (`feat:`, `fix:`, `chore:`, etc.) and without sign-off lines.
- Sign all commits with GPG/SSH (`git commit -S`).

---

## Contributor License Agreement (CLA)

Because this project is dual-licensed (free for personal use, paid commercial license for businesses and organizations), by submitting a Pull Request, patch, or other contribution to this repository, you agree that:

1. Your contribution is your original work and you have the legal right to submit it.
2. You retain copyright to your contribution, and you grant the project author (`gyengus`) a perpetual, worldwide, non-exclusive, royalty-free, irrevocable license to use, reproduce, modify, distribute, and **relicense** your contribution under both the Personal & Non-Commercial License in [`LICENSE`](LICENSE) and any Commercial Licenses offered by the author.
