#!/usr/bin/env python3
"""
Pre-renders Markdown documentation (README, CONTRIBUTING, CHANGELOG, LICENSE)
and synchronizes shared site components (header, footer, analytics)
from templates/ into static HTML files for deployment and local preview.

Benefits:
- Eliminates code duplication: Header, footer, nav, and analytics are maintained in one place (templates/).
- 100% crawlable, pre-rendered semantic HTML with unique metadata for SEO.
- Zero client-side markdown parsing or layout shift (CLS).
- Seamless link resolution between GitHub repository and static website.
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


def render_template(template_str: str, context: dict) -> str:
    """Replaces {{key}} placeholders with context values."""
    result = template_str
    for key, value in context.items():
        result = result.replace(f"{{{{{key}}}}}", str(value))
    return result


def load_templates(project_dir: Path) -> dict:
    tpl_dir = project_dir / "templates"
    return {
        "header": (tpl_dir / "header.html").read_text(encoding="utf-8"),
        "footer": (tpl_dir / "footer.html").read_text(encoding="utf-8"),
        "analytics": (tpl_dir / "analytics.html").read_text(encoding="utf-8"),
        "doc_layout": (tpl_dir / "doc_layout.html").read_text(encoding="utf-8"),
    }


def get_header_html(tpl_header: str, active_nav: str = "", has_changelog: bool = False) -> str:
    if has_changelog:
        active_attr = ' aria-current="page"' if active_nav == "changelog" else ""
        nav_changelog_item = f'<a href="./changelog.html"{active_attr}>Changelog</a>'
    else:
        nav_changelog_item = ""

    context = {
        "nav_overview_attr": ' aria-current="page"' if active_nav == "overview" else "",
        "nav_readme_attr": ' aria-current="page"' if active_nav == "readme" else "",
        "nav_api_attr": ' aria-current="page"' if active_nav == "api" else "",
        "nav_contributing_attr": ' aria-current="page"' if active_nav == "contributing" else "",
        "nav_license_attr": ' aria-current="page"' if active_nav == "license" else "",
        "nav_changelog_item": nav_changelog_item,
    }
    return render_template(tpl_header, context)


def get_footer_html(tpl_footer: str, has_changelog: bool = False) -> str:
    footer_changelog_item = '<a href="./changelog.html">Changelog</a>' if has_changelog else ""
    return render_template(tpl_footer, {"footer_changelog_item": footer_changelog_item})


def sync_components_in_file(file_path: Path, header_html: str, footer_html: str, analytics_html: str) -> bool:
    """Updates <!-- COMPONENT: ... --> blocks inside an HTML file."""
    if not file_path.exists():
        return False

    content = file_path.read_text(encoding="utf-8")
    original = content

    if "<!-- COMPONENT: HEADER -->" in content:
        content = re.sub(
            r"<!-- COMPONENT: HEADER -->.*?<!-- /COMPONENT: HEADER -->",
            f"<!-- COMPONENT: HEADER -->\n  {header_html.strip()}\n  <!-- /COMPONENT: HEADER -->",
            content,
            flags=re.DOTALL,
        )

    if "<!-- COMPONENT: FOOTER -->" in content:
        content = re.sub(
            r"<!-- COMPONENT: FOOTER -->.*?<!-- /COMPONENT: FOOTER -->",
            f"<!-- COMPONENT: FOOTER -->\n  {footer_html.strip()}\n  <!-- /COMPONENT: FOOTER -->",
            content,
            flags=re.DOTALL,
        )

    if "<!-- COMPONENT: ANALYTICS -->" in content:
        content = re.sub(
            r"<!-- COMPONENT: ANALYTICS -->.*?<!-- /COMPONENT: ANALYTICS -->",
            f"<!-- COMPONENT: ANALYTICS -->\n  {analytics_html.strip()}\n  <!-- /COMPONENT: ANALYTICS -->",
            content,
            flags=re.DOTALL,
        )

    if content != original:
        file_path.write_text(content, encoding="utf-8")
        print(f"[SYNC] Updated shared components in {file_path}")
        return True
    return False


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
            a["href"] = "./readme.html"
        elif href in ("CONTRIBUTING.md", "./CONTRIBUTING.md"):
            a["href"] = "./contributing.html"
        elif href in ("CHANGELOG.md", "./CHANGELOG.md"):
            a["href"] = "./changelog.html"
        elif href in ("LICENSE", "./LICENSE"):
            a["href"] = "./license.html"
        elif href in ("docs/api/index.html", "./docs/api/index.html", "docs/api/"):
            a["href"] = "./api/"
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

    templates = load_templates(project_dir)

    # 1. Synchronize components in docs/index.html (and output_dir/index.html if distinct)
    overview_header = get_header_html(templates["header"], active_nav="overview", has_changelog=has_changelog)
    footer_html = get_footer_html(templates["footer"], has_changelog=has_changelog)
    analytics_html = templates["analytics"]

    sync_components_in_file(project_dir / "docs" / "index.html", overview_header, footer_html, analytics_html)
    if output_dir != (project_dir / "docs"):
        sync_components_in_file(output_dir / "index.html", overview_header, footer_html, analytics_html)

    # 2. Pre-render markdown docs
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

        canonical_url = f"{SITE_BASE_URL}{spec['output']}"
        doc_header = get_header_html(templates["header"], active_nav=key, has_changelog=has_changelog)

        page_context = {
            "title": spec["title"],
            "description": spec["description"],
            "canonical_url": canonical_url,
            "analytics": analytics_html,
            "header": doc_header,
            "content": inner_content,
            "footer": footer_html,
        }

        page_html = render_template(templates["doc_layout"], page_context)

        out_file = output_dir / spec["output"]
        out_file.write_text(page_html, encoding="utf-8")
        print(f"[PRE-RENDER] {spec['source']} -> {out_file} ({len(page_html)} bytes)")


if __name__ == "__main__":
    proj_dir = Path(__file__).resolve().parent.parent
    if len(sys.argv) > 1:
        out_target = Path(sys.argv[1]).resolve()
    else:
        out_target = proj_dir / "docs"

    render_all(proj_dir, out_target)
