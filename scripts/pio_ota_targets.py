Import("env")
import os
import io
import re
import subprocess
import zipfile
import urllib.request
import urllib.error

def minify_html(text):
    text = re.sub(r'<!--(?!\s*\[if).*?-->', '', text, flags=re.DOTALL)
    text = re.sub(r'>\s+<', '><', text)
    text = re.sub(r'\s+', ' ', text)
    return text.strip()

def minify_css(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'\s+', ' ', text)
    text = re.sub(r'\s*([:;{}])\s*', r'\1', text)
    text = re.sub(r';}', '}', text)
    return text.strip()

def minify_js_fallback(text):
    token_spec = [
        ('STRING1', r'\"([^\\\n]|(\\.))*?\"'),
        ('STRING2', r'\'([^\\\n]|(\\.))*?\''),
        ('TEMPLATE', r'\`([^\\\`]|(\\.))*?\`'),
        ('BLOCK_COMMENT', r'/\*[\s\S]*?\*/'),
        ('LINE_COMMENT', r'//[^\n]*'),
        ('SPACE', r'\s+'),
        ('OTHER', r'[^\"\'\`/\s]+|/(?![/*])'),
    ]
    tok_regex = '|'.join('(?P<%s>%s)' % pair for pair in token_spec)
    tokens = []
    for mo in re.finditer(tok_regex, text):
        kind = mo.lastgroup
        val = mo.group()
        if kind in ('BLOCK_COMMENT', 'LINE_COMMENT'):
            continue
        tokens.append((kind, val))
    
    out = []
    for i, (kind, val) in enumerate(tokens):
        if kind == 'SPACE':
            prev_tok = tokens[i-1] if i > 0 else (None, '')
            next_tok = tokens[i+1] if i + 1 < len(tokens) else (None, '')
            if prev_tok[1] and next_tok[1]:
                p = prev_tok[1][-1]
                n = next_tok[1][0]
                if (p.isalnum() or p in '_$') and (n.isalnum() or n in '_$'):
                    out.append(' ')
                elif (p in '+-' and p == n):
                    out.append(' ')
        else:
            out.append(val)
    return ''.join(out)

def minify_js(text, project_dir):
    candidates = [
        os.path.join(project_dir, "web", "node_modules", ".bin", "terser"),
        "terser",
    ]
    for cmd in candidates:
        if cmd == "terser" or os.path.isfile(cmd):
            try:
                proc = subprocess.run(
                    [cmd, "--compress", "--mangle"],
                    input=text.encode("utf-8"),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=True,
                )
                res = proc.stdout.decode("utf-8")
                if res.strip():
                    return res
            except Exception:
                pass
    return minify_js_fallback(text)

def process_file_content(full_path, rel_path, project_dir):
    with open(full_path, "rb") as f:
        raw_bytes = f.read()

    lower = rel_path.lower()
    if lower.endswith(".html"):
        try:
            minified = minify_html(raw_bytes.decode("utf-8"))
            return minified.encode("utf-8")
        except Exception:
            return raw_bytes
    elif lower.endswith(".css") and not lower.endswith(".min.css"):
        try:
            minified = minify_css(raw_bytes.decode("utf-8"))
            return minified.encode("utf-8")
        except Exception:
            return raw_bytes
    elif lower.endswith(".js") and not lower.endswith(".min.js"):
        try:
            minified = minify_js(raw_bytes.decode("utf-8"), project_dir)
            return minified.encode("utf-8")
        except Exception:
            return raw_bytes
    return raw_bytes

def get_web_files(web_dir):
    skip_dirs = {"node_modules", "tests", "coverage", ".git", ".vite"}
    skip_exts = {".map", ".test.js"}
    skip_files = {"package.json", "package-lock.json", "vitest.config.js", "fileManager copy.html"}
    
    file_list = []
    for root, dirs, files in os.walk(web_dir):
        dirs[:] = [d for d in dirs if d not in skip_dirs]
        for f in files:
            if f in skip_files:
                continue
            if any(f.endswith(ext) for ext in skip_exts):
                continue
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, web_dir)
            file_list.append((full_path, rel_path))
    return file_list

def create_web_zip_bytes(web_dir, project_dir):
    buf = io.BytesIO()
    total_orig = 0
    total_mini = 0
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for full_path, rel_path in get_web_files(web_dir):
            orig_size = os.path.getsize(full_path)
            content = process_file_content(full_path, rel_path, project_dir)
            zf.writestr(rel_path, content)
            total_orig += orig_size
            total_mini += len(content)
            if orig_size != len(content):
                pct = 100 - (len(content) * 100 // orig_size)
                print(f"  [MINIFY] {rel_path}: {orig_size} -> {len(content)} bytes (-{pct}%)")
            else:
                print(f"  [ASSET]  {rel_path}: {orig_size} bytes")
    print(f"[PACK_WEB] Total assets size: {total_orig} -> {total_mini} bytes")
    return buf.getvalue()

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
    esp_host = os.environ.get("ESP_IP", "esp32_service_uptime_monitor.local")
    url = f"http://{esp_host}/api/web/upload-archive"

    print(f"\n[UPLOAD_WEB] Packaging and uploading web assets to: {url}")
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
