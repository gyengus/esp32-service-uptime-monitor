#!/usr/bin/env python3
"""
Build and preview the complete static site locally.

What this script does:
1. Runs scripts/render_docs.py to pre-render README, CONTRIBUTING, CHANGELOG, and LICENSE into HTML.
2. Ensures all static assets (docs/, CNAME, sitemap, robots, api spec) are assembled in _site/.
3. (Optional) Copies firmware dist/*.bin images if already built.
4. Starts a local HTTP server and opens http://localhost:8765/ in your browser.

Usage:
    python3 scripts/preview_site.py
    python3 scripts/preview_site.py --port 8080
    python3 scripts/preview_site.py --no-browser
    python3 scripts/preview_site.py --build-only
"""

import argparse
import http.server
import os
import shutil
import socketserver
import sys
import webbrowser
from pathlib import Path

# Add scripts directory to path to import render_docs
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))

try:
    import render_docs
except ImportError:
    print("[ERROR] Could not import scripts/render_docs.py")
    sys.exit(1)


def assemble_site(project_dir: Path, out_dir: Path) -> Path:
    print(f"[BUILD] Assembling full static site in {out_dir}...")
    if out_dir.exists():
        shutil.rmtree(out_dir)

    out_dir.mkdir(parents=True, exist_ok=True)
    content_dir = out_dir / "content"
    content_dir.mkdir(parents=True, exist_ok=True)
    firmware_dir = out_dir / "firmware"
    firmware_dir.mkdir(parents=True, exist_ok=True)

    # 1. Copy docs/ contents
    docs_dir = project_dir / "docs"
    if docs_dir.exists():
        for item in docs_dir.iterdir():
            dest = out_dir / item.name
            if item.is_dir():
                shutil.copytree(item, dest)
            else:
                shutil.copy2(item, dest)

    # 2. Pre-render documentation HTML directly into the site root
    print("[BUILD] Pre-rendering Markdown documentation to static HTML...")
    render_docs.render_all(project_dir, out_dir)

    # 3. Copy source markdown files to content/ for fallback readers
    for doc_name in ["README.md", "CONTRIBUTING.md", "LICENSE", "CHANGELOG.md"]:
        src_file = project_dir / doc_name
        if src_file.exists():
            shutil.copy2(src_file, content_dir / doc_name)

    # 4. Copy dist/*.bin and manifests if available
    dist_dir = project_dir / "dist"
    if dist_dir.exists():
        for bin_file in dist_dir.glob("*.bin"):
            shutil.copy2(bin_file, firmware_dir / bin_file.name)
        manifest_file = dist_dir / "manifest.json"
        if manifest_file.exists():
            shutil.copy2(manifest_file, firmware_dir / "manifest.json")

    print("[BUILD] Static site assembled successfully.")
    return out_dir


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=str(directory), **kwargs)

    def log_message(self, format, *args):
        # Concise logging
        sys.stderr.write(f"[HTTP {self.log_date_time_string()}] {args[0]} {args[1]}\n")


def serve_site(site_dir: Path, port: int = 8765, open_browser: bool = True):
    url = f"http://localhost:{port}/"
    print("\n" + "=" * 60)
    print(f"  ESP32 Service Uptime Monitor — Local Site Preview")
    print(f"  Serving directory: {site_dir}")
    print(f"  Local URL:         {url}")
    print("=" * 60)
    print("Press Ctrl+C to stop the server.\n")

    if open_browser:
        try:
            webbrowser.open(url)
        except Exception:
            pass

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", port), lambda *args, **kwargs: Handler(*args, directory=site_dir, **kwargs)) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[STOP] Shutting down preview server.")


def main():
    parser = argparse.ArgumentParser(description="Build and preview ESP32 Service Uptime Monitor static site.")
    parser.add_argument("--port", type=int, default=8765, help="Port to serve on (default: 8765)")
    parser.add_argument("--out-dir", type=str, default=str(PROJECT_DIR / "_site"), help="Output directory")
    parser.add_argument("--no-browser", action="store_true", help="Do not open browser automatically")
    parser.add_argument("--build-only", action="store_true", help="Only assemble the site without starting the HTTP server")
    args = parser.parse_args()

    out_path = Path(args.out_dir).resolve()
    assemble_site(PROJECT_DIR, out_path)

    if not args.build_only:
        serve_site(out_path, port=args.port, open_browser=not args.no_browser)


if __name__ == "__main__":
    main()
