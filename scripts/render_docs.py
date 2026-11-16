#!/usr/bin/env python3
"""
Pre-renders Markdown documentation (README, CONTRIBUTING, CHANGELOG, LICENSE)
into clean, static HTML files for GitHub Pages deployment.

Benefits:
- Eliminates client-side JS markdown parsing and layout shift (CLS).
- Provides 100% crawlable, pre-rendered semantic HTML with unique metadata for SEO.
- Resolves GitHub repo links (e.g. include/devices.h, docs/api/index.html, LICENSE)
  so they work seamlessly both on the static website and on GitHub.
"""

import os
import re
import sys
from pathlib import Path
import markdown
from bs4 import BeautifulSoup

REPO_BASE_URL = "https://github.com/gyengus/esp32-service-uptime-monitor/blob/master/"
SITE_BASE_URL = "https://esp32-service-uptime-monitor.com/"

DOCS_CONFIG = {
    "readme": {
        "source": "README.md",
        "output": "readme.html",
        "title": "Documentation — ESP32 Service Uptime Monitor",
        "description": "Comprehensive documentation for ESP32 Service Uptime Monitor: multi-board setup, hardware pinouts, REST API, Web UI, and security.",
        "nav_key": "readme",
    },
    "contributing": {
        "source": "CONTRIBUTING.md",
        "output": "contributing.html",
        "title": "Contributing & Development Guide — ESP32 Service Uptime Monitor",
        "description": "Development workflow, PlatformIO build targets, OTA flashing, test suites, and coding standards for ESP32 Service Uptime Monitor.",
        "nav_key": "contributing",
    },
    "changelog": {
        "source": "CHANGELOG.md",
        "output": "changelog.html",
        "title": "Changelog — ESP32 Service Uptime Monitor",
        "description": "Release history and version changelog for ESP32 Service Uptime Monitor firmware and Web UI.",
        "nav_key": "changelog",
    },
    "license": {
        "source": "LICENSE",
        "output": "license.html",
        "title": "License (Personal & Commercial) — ESP32 Service Uptime Monitor",
        "description": "Dual licensing terms for ESP32 Service Uptime Monitor: free for personal homelabs with a 30-day evaluation trial for organizations.",
        "nav_key": "license",
        "is_plain": True,
    },
}

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>{title}</title>
  <meta name="description" content="{description}" />
  <link rel="canonical" href="{canonical_url}" />
  <meta property="og:type" content="article" />
  <meta property="og:site_name" content="ESP32 Service Uptime Monitor" />
  <meta property="og:title" content="{title}" />
  <meta property="og:description" content="{description}" />
  <meta property="og:url" content="{canonical_url}" />
  <meta name="twitter:card" content="summary" />
  <meta name="twitter:title" content="{title}" />
  <meta name="twitter:description" content="{description}" />
  <link rel="stylesheet" href="./styles.css" />
  <script async src="https://www.googletagmanager.com/gtm.js?id=GTM-PGXR4C43"></script>
  <script>
    window.dataLayer = window.dataLayer || [];
    function gtag(){{dataLayer.push(arguments);}}
    gtag('js', new Date());
  </script>
</head>
<body>
  <header class="site-header">
    <div class="container nav-inner">
      <a class="brand" href="./">
        <span class="status-dot" aria-hidden="true"></span>
        <span>ESP32 Service Uptime Monitor</span>
      </a>
      <nav class="nav-links" aria-label="Main navigation">
        <a href="./">Overview &amp; Flasher</a>
        <a href="./docs.html?doc=readme"{nav_readme_attr}>Documentation</a>
        <a href="./docs.html?doc=api"{nav_api_attr}>API (Swagger)</a>
        <a href="./docs.html?doc=contributing"{nav_contributing_attr}>Contributing</a>
        {nav_changelog_item}
        <a href="./docs.html?doc=license"{nav_license_attr}>License</a>
        <a href="https://github.com/gyengus/esp32-service-uptime-monitor/releases" target="_blank" rel="noopener">Releases</a>
        <a href="https://github.com/gyengus/esp32-service-uptime-monitor" target="_blank" rel="noopener">GitHub</a>
        <button type="button" id="themeToggle" class="theme-toggle" aria-label="Toggle color theme" title="Toggle color theme">
          <svg id="themeIcon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"></svg>
        </button>
      </nav>
    </div>
  </header>

  <main class="container docs-layout">
    <article id="docContent" class="markdown-body">
      {content}
    </article>
  </main>

  <footer class="site-footer">
    <div class="container">
      <div class="footer-links">
        <a href="./">Overview &amp; Flasher</a>
        <a href="./docs.html?doc=readme">Documentation</a>
        <a href="./docs.html?doc=api">API (Swagger)</a>
        <a href="./docs.html?doc=contributing">Contributing</a>
        {footer_changelog_item}
        <a href="./docs.html?doc=license">License</a>
        <a href="https://github.com/gyengus/esp32-service-uptime-monitor/releases" target="_blank" rel="noopener">Releases</a>
        <a href="https://github.com/gyengus/esp32-service-uptime-monitor" target="_blank" rel="noopener">GitHub</a>
        <span class="footer-author">Created by <a href="https://gyengus.hu/?utm_source=uptimeMonitorPage" target="_blank" rel="noopener">Gyengus</a></span>
      </div>
    </div>
  </footer>

  <script>
    const themeToggle = document.getElementById("themeToggle");
    const themeIcon = document.getElementById("themeIcon");
    const THEME_KEY = "uptime-docs-theme";
    const SUN_SVG = '<circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/>';
    const MOON_SVG = '<path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z"/>';

    function getEffectiveTheme() {{
      const saved = localStorage.getItem(THEME_KEY);
      if (saved === "light" || saved === "dark") return saved;
      return window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
    }}

    function applyTheme(theme) {{
      document.documentElement.setAttribute("data-theme", theme);
      themeIcon.innerHTML = theme === "light" ? MOON_SVG : SUN_SVG;
    }}

    applyTheme(getEffectiveTheme());
    themeToggle.addEventListener("click", () => {{
      const next = getEffectiveTheme() === "light" ? "dark" : "light";
      localStorage.setItem(THEME_KEY, next);
      applyTheme(next);
    }});
  </script>
</body>
</html>
"""


def fix_html_links_and_alerts(html_content: str) -> str:
    soup = BeautifulSoup(html_content, "html.parser")

    # 1. Transform GitHub-style alerts:
    # <blockquote><p>[!TIP]\n<strong>...
    for bq in soup.find_all("blockquote"):
        p = bq.find("p")
        if p and p.text:
            match = re.match(r"^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*", p.text)
            if match:
                alert_type = match.group(1).lower()
                # Remove [!TYPE] marker from the text
                for child in list(p.children):
                    if isinstance(child, str):
                        new_text = re.sub(r"^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*", "", child)
                        child.replace_with(new_text)
                        break
                bq["class"] = bq.get("class", []) + ["markdown-alert", f"markdown-alert-{alert_type}"]

    # 2. Fix links
    for a in soup.find_all("a", href=True):
        href = a["href"]

        # Inter-doc links on site:
        if href in ("README.md", "./README.md"):
            a["href"] = "./docs.html?doc=readme"
        elif href in ("CONTRIBUTING.md", "./CONTRIBUTING.md"):
            a["href"] = "./docs.html?doc=contributing"
        elif href in ("CHANGELOG.md", "./CHANGELOG.md"):
            a["href"] = "./docs.html?doc=changelog"
        elif href in ("LICENSE", "./LICENSE"):
            a["href"] = "./docs.html?doc=license"
        elif href in ("docs/api/index.html", "./docs/api/index.html", "docs/api/"):
            a["href"] = "./docs.html?doc=api"
        elif href.startswith("docs/api/"):
            # e.g. docs/api/openapi.yaml -> ./api/openapi.yaml
            a["href"] = "./api/" + href[len("docs/api/"):]
        elif not href.startswith(("http://", "https://", "#", "mailto:")):
            # Code reference links (e.g. include/devices.h, src/Api.cpp)
            # Resolve against GitHub repo master branch so users can view the actual code
            a["href"] = REPO_BASE_URL + href.lstrip("./")
            a["target"] = "_blank"
            a["rel"] = "noopener noreferrer"

    return str(soup)


def render_all(project_dir: Path, output_dir: Path):
    output_dir.mkdir(parents=True, exist_ok=True)
    has_changelog = (project_dir / "CHANGELOG.md").exists()

    nav_changelog_item = '<a href="./docs.html?doc=changelog">Changelog</a>' if has_changelog else ""
    footer_changelog_item = '<a href="./docs.html?doc=changelog">Changelog</a>' if has_changelog else ""

    md_converter = markdown.Markdown(extensions=["extra", "tables", "fenced_code", "toc"])

    for key, spec in DOCS_CONFIG.items():
        src_path = project_dir / spec["source"]
        if not src_path.exists():
            continue

        raw_text = src_path.read_text(encoding="utf-8")

        if spec.get("is_plain"):
            escaped = (
                raw_text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
            )
            inner_content = f"<h1>License</h1><pre><code>{escaped}</code></pre>"
        else:
            md_converter.reset()
            inner_html = md_converter.convert(raw_text)
            inner_content = fix_html_links_and_alerts(inner_html)

        canonical_url = f"{SITE_BASE_URL}docs.html?doc={key}"

        page_html = HTML_TEMPLATE.format(
            title=spec["title"],
            description=spec["description"],
            canonical_url=canonical_url,
            nav_readme_attr=' aria-current="page"' if key == "readme" else "",
            nav_api_attr=' aria-current="page"' if key == "api" else "",
            nav_contributing_attr=' aria-current="page"' if key == "contributing" else "",
            nav_license_attr=' aria-current="page"' if key == "license" else "",
            nav_changelog_item=nav_changelog_item,
            footer_changelog_item=footer_changelog_item,
            content=inner_content,
        )

        out_file = output_dir / spec["output"]
        out_file.write_text(page_html, encoding="utf-8")
        print(f"[PRE-RENDER] {spec['source']} -> {out_file} ({len(page_html)} bytes)")


if __name__ == "__main__":
    proj_dir = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 1:
        out_dir = Path(sys.argv[1]).resolve()
    else:
        out_dir = proj_dir / "docs"

    render_all(proj_dir, out_dir)
