import os
import io
import re
import subprocess
import zipfile
import hashlib

def get_firmware_version(project_dir):
    version_file = os.path.join(project_dir, "include", "version.h")
    if os.path.isfile(version_file):
        try:
            with open(version_file, "r", encoding="utf-8") as f:
                content = f.read()
                m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
                if m:
                    return m.group(1)
        except Exception:
            pass
    return "0.0.1"

def get_git_commit_hash(project_dir):
    try:
        out = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=project_dir)
        return out.decode('ascii').strip()
    except Exception:
        return "unknown"

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
                    [cmd, "--compress", "toplevel=true", "--mangle", "toplevel=true"],
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
    skip_files = {"package.json", "package-lock.json", "vitest.config.js", ".version"}
    
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

def create_web_zip_bytes(web_dir, project_dir, verbose=False):
    buf = io.BytesIO()
    total_orig = 0
    total_mini = 0
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for full_path, rel_path in sorted(get_web_files(web_dir), key=lambda x: x[1]):
            orig_size = os.path.getsize(full_path)
            content = process_file_content(full_path, rel_path, project_dir)
            zinfo = zipfile.ZipInfo(filename=rel_path, date_time=(2026, 1, 1, 0, 0, 0))
            zinfo.compress_type = zipfile.ZIP_DEFLATED
            zinfo.external_attr = 0o644 << 16
            zf.writestr(zinfo, content)
            total_orig += orig_size
            total_mini += len(content)
            if verbose:
                if orig_size != len(content):
                    pct = 100 - (len(content) * 100 // orig_size)
                    print(f"  [MINIFY] {rel_path}: {orig_size} -> {len(content)} bytes (-{pct}%)")
                else:
                    print(f"  [ASSET]  {rel_path}: {orig_size} bytes")

        # Inject .version file into root of archive
        version = get_firmware_version(project_dir)
        commit = get_git_commit_hash(project_dir)
        version_str = f"{version}-{commit}\n"
        version_bytes = version_str.encode("utf-8")
        zinfo_ver = zipfile.ZipInfo(filename=".version", date_time=(2026, 1, 1, 0, 0, 0))
        zinfo_ver.compress_type = zipfile.ZIP_DEFLATED
        zinfo_ver.external_attr = 0o644 << 16
        zf.writestr(zinfo_ver, version_bytes)
        total_orig += len(version_bytes)
        total_mini += len(version_bytes)
        if verbose:
            print(f"  [VERSION] .version: {version_str.strip()}")

    if verbose:
        print(f"[PACK_WEB] Total assets size: {total_orig} -> {total_mini} bytes")
    return buf.getvalue()

def ensure_embedded_web_archive(project_dir):
    web_dir = os.path.join(project_dir, "web")
    header_path = os.path.join(project_dir, "include", "EmbeddedWebArchive.h")
    source_path = os.path.join(project_dir, "src", "EmbeddedWebArchive.cpp")

    zip_bytes = create_web_zip_bytes(web_dir, project_dir, verbose=False)
    md5 = hashlib.md5(zip_bytes).hexdigest()

    # Always ensure standalone web.zip matches the embedded archive byte-for-byte
    out_zip = os.path.join(project_dir, "web.zip")
    with open(out_zip, "wb") as f:
        f.write(zip_bytes)

    # Check if existing source file has the same hash
    if os.path.isfile(source_path) and os.path.isfile(header_path):
        with open(source_path, "r", encoding="utf-8") as f:
            first_line = f.readline()
            if first_line.startswith(f"// HASH: {md5}"):
                return False

    header_content = """#ifndef _EMBEDDED_WEB_ARCHIVE_H_
#define _EMBEDDED_WEB_ARCHIVE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t embedded_web_zip[];
extern const size_t embedded_web_zip_len;

#ifdef __cplusplus
}
#endif

#endif // _EMBEDDED_WEB_ARCHIVE_H_
"""
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(header_content)

    lines = []
    for i in range(0, len(zip_bytes), 16):
        chunk = zip_bytes[i:i+16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")

    source_content = f"""// HASH: {md5}
// Auto-generated by scripts/generate_web_embed.py - DO NOT EDIT MANUALLY
#include "EmbeddedWebArchive.h"
#include <Arduino.h>

#if defined(ESP32)
#include <pgmspace.h>
const uint8_t embedded_web_zip[] PROGMEM = {{
{os.linesep.join(lines)}
}};
#else
const uint8_t embedded_web_zip[] = {{
{os.linesep.join(lines)}
}};
#endif

const size_t embedded_web_zip_len = sizeof(embedded_web_zip);
"""
    with open(source_path, "w", encoding="utf-8") as f:
        f.write(source_content)

    print(f"[EMBED_WEB] Generated src/EmbeddedWebArchive.cpp ({len(zip_bytes)} bytes, MD5: {md5})")
    return True
