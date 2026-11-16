#!/usr/bin/env python3
"""
Build and preview the complete static site locally with optional file watching and live reload.

What this script does:
1. Runs scripts/render_docs.py to pre-render README, CONTRIBUTING, CHANGELOG, and LICENSE into HTML.
2. Ensures all static assets (docs/, CNAME, sitemap, robots, api spec) are assembled in _site/.
3. (Optional) Copies firmware dist/*.bin images if already built.
4. Starts a local HTTP server with automatic file watching and browser live reload.

Usage:
    python3 scripts/preview_site.py
    python3 scripts/preview_site.py --port 8080
    python3 scripts/preview_site.py --no-browser
    python3 scripts/preview_site.py --no-watch
    python3 scripts/preview_site.py --build-only
"""

import argparse
import http.server
import json
import os
import queue
import shutil
import sys
import threading
import time
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

reload_subscribers = []
reload_lock = threading.Lock()
server_stop_event = threading.Event()
live_reload_enabled = True

LIVERELOAD_SCRIPT = """
<script>
  (function() {
    let source = new EventSource("/__livereload");
    source.onmessage = function(e) {
      if (e.data === "reload") {
        console.log("[LiveReload] File change detected, reloading page...");
        window.location.reload();
      }
    };
    source.onerror = function() {
      source.close();
      setTimeout(function() {
        source = new EventSource("/__livereload");
      }, 1000);
    };
  })();
</script>
"""


def notify_clients():
    with reload_lock:
        for q in reload_subscribers:
            q.put("reload")


def assemble_site(project_dir: Path, out_dir: Path) -> Path:
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
                shutil.copytree(item, dest, dirs_exist_ok=True)
            else:
                shutil.copy2(item, dest)

    # 1.5 Ensure CHANGELOG.md exists for documentation pre-rendering
    changelog_path = project_dir / "CHANGELOG.md"
    if not changelog_path.exists():
        try:
            import changelog
            changelog_text = changelog.generate_full_changelog()
            changelog_path.write_text(changelog_text, encoding="utf-8")
        except Exception:
            pass

    # 2. Pre-render documentation HTML directly into the site root
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
            try:
                import git_version
                firmware_version = git_version.get_firmware_version()
            except Exception:
                firmware_version = os.environ.get("CI_COMMIT_TAG") or "latest"

            try:
                data = json.loads(manifest_file.read_text(encoding="utf-8"))
                data["version"] = firmware_version
                (firmware_dir / "manifest.json").write_text(
                    json.dumps(data, indent=2),
                    encoding="utf-8",
                )
                for build in data.get("builds", []):
                    parts = build.get("parts", [])
                    if not parts:
                        continue
                    bin_path = parts[0].get("path", "")
                    env_name = bin_path.removesuffix(".bin")
                    board_manifest = {
                        "name": f"ESP32 Service Uptime Monitor ({env_name})",
                        "version": firmware_version,
                        "new_install_prompt_erase": True,
                        "builds": [build],
                    }
                    (firmware_dir / f"manifest-{env_name}.json").write_text(
                        json.dumps(board_manifest, indent=2),
                        encoding="utf-8",
                    )
            except Exception:
                shutil.copy2(manifest_file, firmware_dir / "manifest.json")

    # 5. Generate test and coverage badge endpoints
    try:
        import combine_coverage
        combine_coverage.generate_badges(project_dir, out_dir)
    except Exception:
        pass

    return out_dir


def get_watch_files(project_dir: Path):
    files = []
    for name in ["README.md", "CONTRIBUTING.md", "LICENSE", "CHANGELOG.md"]:
        f = project_dir / name
        if f.exists():
            files.append(f)

    docs_dir = project_dir / "docs"
    if docs_dir.exists():
        for p in docs_dir.rglob("*"):
            if p.is_file():
                files.append(p)

    tpl_dir = project_dir / "templates"
    if tpl_dir.exists():
        for p in tpl_dir.rglob("*"):
            if p.is_file():
                files.append(p)

    return files


def watch_and_rebuild(project_dir: Path, out_dir: Path, stop_event: threading.Event):
    mtimes = {}
    for f in get_watch_files(project_dir):
        try:
            mtimes[f] = f.stat().st_mtime
        except OSError:
            pass

    while not stop_event.is_set():
        time.sleep(0.5)
        changed_file = None
        current_files = get_watch_files(project_dir)
        for f in current_files:
            try:
                mt = f.stat().st_mtime
                if f not in mtimes or mtimes[f] != mt:
                    changed_file = f
                    mtimes[f] = mt
                    break
            except OSError:
                pass

        if changed_file:
            rel_name = changed_file.relative_to(project_dir) if changed_file.is_relative_to(project_dir) else changed_file
            print(f"\n[WATCHER] Change detected in {rel_name}, rebuilding site...")
            try:
                assemble_site(project_dir, out_dir)
                print("[WATCHER] Rebuild complete. Triggering live reload in browser...\n")
                notify_clients()
            except Exception as e:
                print(f"[WATCHER ERROR] Failed to rebuild: {e}")


class LiveReloadHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, directory=None, **kwargs):
        super().__init__(*args, directory=str(directory), **kwargs)

    def log_message(self, format, *args):
        # Silence SSE poll logging to keep terminal clean
        if args and str(args[0]).startswith("GET /__livereload"):
            return
        sys.stderr.write(f"[HTTP {self.log_date_time_string()}] {args[0]} {args[1]}\n")

    def end_headers(self):
        # Disable browser caching during local preview
        self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()

    def do_GET(self):
        if self.path == "/__livereload":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()

            q = queue.Queue()
            with reload_lock:
                reload_subscribers.append(q)

            try:
                self.wfile.write(b": connected\n\n")
                self.wfile.flush()
                while not server_stop_event.is_set():
                    try:
                        msg = q.get(timeout=1.0)
                        self.wfile.write(f"data: {msg}\n\n".encode("utf-8"))
                        self.wfile.flush()
                    except queue.Empty:
                        self.wfile.write(b": keep-alive\n\n")
                        self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            finally:
                with reload_lock:
                    if q in reload_subscribers:
                        reload_subscribers.remove(q)
            return

        # Inject LiveReload script for HTML documents
        path = Path(self.translate_path(self.path))
        if path.is_dir():
            path = path / "index.html"

        if path.is_file() and path.suffix.lower() == ".html" and live_reload_enabled:
            try:
                content = path.read_text(encoding="utf-8")
                if "</body>" in content:
                    content = content.replace("</body>", f"{LIVERELOAD_SCRIPT}\n</body>", 1)
                else:
                    content += LIVERELOAD_SCRIPT

                encoded = content.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(encoded)))
                self.end_headers()
                self.wfile.write(encoded)
                return
            except Exception:
                pass

        super().do_GET()


def serve_site(site_dir: Path, port: int = 8765, open_browser: bool = True, watch: bool = True):
    global live_reload_enabled
    live_reload_enabled = watch

    url = f"http://localhost:{port}/"
    print("\n" + "=" * 65)
    print("  ESP32 Service Uptime Monitor — Local Site Preview")
    print(f"  Serving directory: {site_dir}")
    print(f"  Local URL:         {url}")
    print(f"  Live Reload:       {'Enabled (Watching docs/, templates/, *.md)' if watch else 'Disabled'}")
    print("=" * 65)
    print("Press Ctrl+C to stop the server.\n")

    watcher_thread = None
    if watch:
        watcher_thread = threading.Thread(
            target=watch_and_rebuild,
            args=(PROJECT_DIR, site_dir, server_stop_event),
            daemon=True,
        )
        watcher_thread.start()

    if open_browser:
        try:
            webbrowser.open(url)
        except Exception:
            pass

    http.server.ThreadingHTTPServer.allow_reuse_address = True
    with http.server.ThreadingHTTPServer(
        ("", port),
        lambda *args, **kwargs: LiveReloadHandler(*args, directory=site_dir, **kwargs)
    ) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[STOP] Shutting down preview server.")
        finally:
            server_stop_event.set()


def main():
    parser = argparse.ArgumentParser(description="Build and preview ESP32 Service Uptime Monitor static site.")
    parser.add_argument("--port", type=int, default=8765, help="Port to serve on (default: 8765)")
    parser.add_argument("--out-dir", type=str, default=str(PROJECT_DIR / "_site"), help="Output directory")
    parser.add_argument("--no-browser", action="store_true", help="Do not open browser automatically")
    parser.add_argument("--no-watch", action="store_true", help="Disable file watching and browser live reload")
    parser.add_argument("--build-only", action="store_true", help="Only assemble the site without starting the HTTP server")
    args = parser.parse_args()

    out_path = Path(args.out_dir).resolve()
    print(f"[BUILD] Assembling full static site in {out_path}...")
    assemble_site(PROJECT_DIR, out_path)
    print("[BUILD] Static site assembled successfully.")

    if not args.build_only:
        serve_site(out_path, port=args.port, open_browser=not args.no_browser, watch=not args.no_watch)


if __name__ == "__main__":
    main()
