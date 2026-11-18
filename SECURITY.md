# Security Policy

The security of **ESP32 Service Uptime Monitor** is taken seriously. As a self-hosted monitoring appliance and embedded web service, safeguarding device integrity, local network boundaries, and user credentials is vital.

---

## Supported Versions

Security updates and vulnerability patches are actively maintained for the following versions:

| Version / Branch | Supported | Notes |
| :--- | :--- | :--- |
| `master` branch | :white_check_mark: | Latest development code and rolling fixes |
| Latest release (v0.x / v1.x) | :white_check_mark: | Official release firmware binaries |
| Older release tags | :x: | Please upgrade to the latest official release |

---

## Reporting a Vulnerability

If you discover a potential security vulnerability, please do **not** disclose it publicly via GitHub issues, pull requests, or public discussions.

### Preferred Reporting Channels

1. **GitHub Private Vulnerability Reporting:**
   If enabled on this repository, navigate to the **Security** tab and click **"Report a vulnerability"** to open a confidential security advisory.

2. **Official Security Contact Portal:**
   Submit details via our encrypted contact form at:
   [https://esp32-service-uptime-monitor.com/#contact](https://esp32-service-uptime-monitor.com/#contact)  
   Please select **Security / Vulnerability Report** as the inquiry type.

### What to Include

To help us assess and resolve the issue quickly, please provide:
- A description of the vulnerability and its potential impact.
- Affected component(s) (e.g., REST API, Web UI, OTA update mechanism, Wi-Fi credentials storage).
- Step-by-step reproduction instructions or a minimal Proof of Concept (PoC).
- ESP32 hardware model, chip revision, and firmware version/commit hash used in testing.

---

## Response and Disclosure Process

- **Acknowledgement:** We aim to acknowledge vulnerability submissions within 48 hours.
- **Assessment:** We will assess severity, verify reproduction steps, and coordinate with the reporter.
- **Resolution:** A fix will be developed in a private branch, verified, and released with an advisory.
- **Credit:** Reporters will be credited in release notes and security advisories unless they request to remain anonymous.
