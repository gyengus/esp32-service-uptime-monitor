Import("env")
import os
import sys
import io
import urllib.request
import urllib.error

project_dir = env.get("PROJECT_DIR", ".")
scripts_dir = os.path.join(project_dir, "scripts")
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)

import web_packer

def resolve_target_host(env):
    """
    Resolve the target device host/IP in order of priority:
    1. CLI / environment variables: ESP_HOST, ESP_IP
    2. PlatformIO upload_port (from platformio.ini or --upload-port CLI)
    3. Hostname from local config.json if present
    4. Default RFC-compliant hostname: esp32-service-uptime-monitor.local
    """
    for var in ("ESP_HOST", "ESP_IP"):
        val = os.environ.get(var)
        if val and val.strip():
            return val.strip()

    upload_port = env.get("UPLOAD_PORT")
    if upload_port:
        upload_port = str(upload_port).strip()
        if upload_port and not upload_port.startswith("/dev/"):
            return upload_port

    project_dir = env.get("PROJECT_DIR", ".")
    for candidate in ("config.json", os.path.join("data", "config.json")):
        cfg_path = os.path.join(project_dir, candidate)
        if os.path.isfile(cfg_path):
            try:
                import json
                with open(cfg_path, "r", encoding="utf-8") as f:
                    cfg_data = json.load(f)
                    host = cfg_data.get("hostname")
                    if host and isinstance(host, str) and host.strip():
                        host = host.strip()
                        if not host.endswith(".local") and not any(c.isdigit() for c in host.split(".")) and ":" not in host:
                            return f"{host}.local"
                        return host
            except Exception:
                pass

    return "esp32-service-uptime-monitor.local"

def create_web_zip_bytes(web_dir, project_dir):
    return web_packer.create_web_zip_bytes(web_dir, project_dir, verbose=True)

def pack_web_action(source, target, env):
    project_dir = env.get("PROJECT_DIR", ".")
    web_dir = os.path.join(project_dir, "web")
    out_zip = os.path.join(project_dir, "web.zip")
    print(f"\n[PACK_WEB] Packaging web assets from {web_dir} -> {out_zip}")
    zip_bytes = create_web_zip_bytes(web_dir, project_dir)
    with open(out_zip, "wb") as f:
        f.write(zip_bytes)
    print(f"[PACK_WEB] Created {out_zip} ({len(zip_bytes)} bytes)\n")

def upload_web_action(source, target, env):
    project_dir = env.get("PROJECT_DIR", ".")
    web_dir = os.path.join(project_dir, "web")
    esp_host = resolve_target_host(env)
    url = f"http://{esp_host}/api/web/upload-archive"

    print(f"\n[UPLOAD_WEB] Target host: {esp_host}")
    print(f"[UPLOAD_WEB] Packaging and uploading web assets to: {url}")
    zip_bytes = create_web_zip_bytes(web_dir, project_dir)
    print(f"[UPLOAD_WEB] Archive size: {len(zip_bytes)} bytes")

    boundary = "----WebKitFormBoundaryPlatformIO7MA4YWxkTrZu0gW"
    body = bytearray()
    body.extend(f"--{boundary}\r\n".encode("utf-8"))
    body.extend(b'Content-Disposition: form-data; name="archive"; filename="web.zip"\r\n')
    body.extend(b'Content-Type: application/zip\r\n\r\n')
    body.extend(zip_bytes)
    body.extend(f"\r\n--{boundary}--\r\n".encode("utf-8"))

    req = urllib.request.Request(url, data=bytes(body), method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    req.add_header("Content-Length", str(len(body)))
    req.add_header("X-PlatformIO-Upload", "1")

    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            resp_body = resp.read().decode("utf-8", errors="replace")
            print(f"[UPLOAD_WEB] Server response ({resp.status}): {resp_body}")
            print("[UPLOAD_WEB] Web UI update successful!\n")
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace")
        print(f"[UPLOAD_WEB] HTTP Error {e.code}: {err_body}")
        raise
    except Exception as e:
        print(f"[UPLOAD_WEB] Error connecting to {url}: {e}")
        print("[UPLOAD_WEB] Tip: Target device can be set via ESP_HOST=<host/ip>, ESP_IP=<ip>, or 'upload_port' in platformio.ini")
        raise

def upload_firmware_ota_action(source, target, env):
    esp_host = resolve_target_host(env)
    firmware_path = str(source[0])
    file_size = os.path.getsize(firmware_path)

    import hashlib
    with open(firmware_path, "rb") as f:
        firmware_bytes = f.read()
    md5 = hashlib.md5(firmware_bytes).hexdigest()

    print(f"\n[OTA_UPLOAD] Target host: {esp_host}")
    print(f"[OTA_UPLOAD] Initiating firmware update to: http://{esp_host}/ ...")
    print(f"[OTA_UPLOAD] Firmware binary: {firmware_path} ({file_size} bytes, MD5: {md5})")

    try:
        import requests
        from requests_toolbelt import MultipartEncoder
    except ImportError:
        env.Execute("$PYTHONEXE -m pip install requests_toolbelt")
        from requests_toolbelt import MultipartEncoder
        import requests

    start_url = f"http://{esp_host}/ota/start?mode=fr&hash={md5}"
    try:
        resp = requests.get(start_url, timeout=15)
        if resp.status_code != 200:
            print(f"[OTA_UPLOAD] Handshake failed: {resp.status_code} {resp.text}")
            raise RuntimeError(f"OTA start failed: {resp.status_code}")
        print(f"[OTA_UPLOAD] Handshake response: {resp.text.strip()}")
    except Exception as e:
        print(f"[OTA_UPLOAD] Error connecting to {start_url}: {e}")
        print("[OTA_UPLOAD] Tip: Target device can be set via ESP_HOST=<host/ip>, ESP_IP=<ip>, or 'upload_port' in platformio.ini")
        raise

    upload_url = f"http://{esp_host}/ota/upload"
    print(f"[OTA_UPLOAD] Streaming firmware upload to {upload_url}...")
    try:
        with open(firmware_path, "rb") as f:
            encoder = MultipartEncoder(fields={
                "MD5": md5,
                "firmware": ("firmware.bin", f, "application/octet-stream")
            })
            headers = {
                "Content-Type": encoder.content_type,
                "Content-Length": str(encoder.len)
            }
            resp = requests.post(upload_url, data=encoder, headers=headers, timeout=90)
            if resp.status_code != 200:
                print(f"[OTA_UPLOAD] Upload failed: {resp.status_code} {resp.text}")
                raise RuntimeError(f"OTA upload failed: {resp.status_code}")
            print(f"[OTA_UPLOAD] Server response: {resp.text.strip()}")
            print("[OTA_UPLOAD] Firmware update successful! ESP32 is rebooting into the new firmware.\n")
    except Exception as e:
        print(f"[OTA_UPLOAD] Upload error: {e}")
        raise

env.AddCustomTarget(
    name="uploadweb",
    dependencies=None,
    actions=[upload_web_action],
    title="Upload Web UI to SD",
    description="Package web/ folder into zip and upload to ESP32 SD card"
)

env.AddCustomTarget(
    name="packweb",
    dependencies=None,
    actions=[pack_web_action],
    title="Pack Web UI to web.zip",
    description="Create web.zip containing all production assets from web/"
)

if env.get("UPLOAD_PROTOCOL") == "custom":
    env.Replace(UPLOADCMD=upload_firmware_ota_action)
