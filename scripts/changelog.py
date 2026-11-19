#!/usr/bin/env python3
"""
Generate release notes or full CHANGELOG.md from git history and tags.

Usage:
    python3 scripts/changelog.py --release-notes [TAG]
    python3 scripts/changelog.py --full
"""

import argparse
import os
import re
import subprocess
import sys
from datetime import datetime


def run_git(args):
    try:
        res = subprocess.check_output(['git'] + args, stderr=subprocess.DEVNULL)
        return res.decode('utf-8', errors='replace').strip()
    except Exception:
        return ""


def get_tags():
    """Returns list of tags sorted by tag creator date (newest first)."""
    raw = run_git(['tag', '--sort=-creatordate'])
    if not raw:
        return []
    return [t.strip() for t in raw.splitlines() if t.strip()]


def get_commit_log(rev_range=None):
    """Returns list of commit subjects and short hashes, omitting merge commits."""
    cmd = ['log', '--no-merges', '--format=%s|%h']
    if rev_range:
        cmd.append(rev_range)
    raw = run_git(cmd)
    if not raw:
        return []

    entries = []
    for line in raw.splitlines():
        if not line.strip() or '|' not in line:
            continue
        subject, short_hash = line.strip().split('|', 1)
        if subject.startswith("Merge branch") or subject.startswith("Merge remote"):
            continue
        entries.append((subject, short_hash))
    return entries


def get_tag_date(tag):
    raw = run_git(['log', '-1', '--format=%ad', '--date=short', tag])
    return raw or datetime.now().strftime("%Y-%m-%d")


def format_entries(entries):
    if not entries:
        return ["* Routine maintenance and internal improvements."]
    return [f"* {subject} ({short_hash})" for subject, short_hash in entries]


def generate_release_notes(target_tag=None):
    tags = get_tags()
    if not target_tag:
        ci_tag = os.environ.get("CI_COMMIT_TAG") or (
            os.environ.get("GITHUB_REF_NAME") if os.environ.get("GITHUB_REF_TYPE") == "tag" else None
        )
        target_tag = ci_tag or (tags[0] if tags else None)

    if not target_tag:
        # Fallback if no tag specified and none found
        entries = get_commit_log()[:20]
        lines = [
            "## What's Changed",
            "",
        ]
        lines.extend(format_entries(entries))
        return "\n".join(lines)

    # Determine previous tag if available
    prev_tag = run_git(['describe', '--tags', '--abbrev=0', f"{target_tag}^"])
    if prev_tag:
        rev_range = f"{prev_tag}..{target_tag}"
    else:
        rev_range = target_tag

    entries = get_commit_log(rev_range)
    clean_name = target_tag.lstrip("v")
    lines = [
        f"## Release {clean_name}",
        "",
    ]

    if not prev_tag:
        lines.extend([
            "### Highlights",
            "",
            "* **Multi-Board ESP32 Support:** Ready-to-flash images for `esp32-s3-cam` (MicroSD), `esp32-s3`, `esp32` (WROOM-32), `esp32-c3`, `esp32-c6` (Wi-Fi 6), and `esp32-c5` (Dual-Band 2.4/5GHz Wi-Fi 6).",
            "* **1-Click Browser Web Flasher & Improv Wi-Fi:** Install directly via Web Serial at [esp32-service-uptime-monitor.com](https://esp32-service-uptime-monitor.com/#flasher) and configure Wi-Fi over USB or Captive Portal AP.",
            "* **Out-of-Band Monitoring & Discord Alerts:** Non-blocking Core 0 HTTP/HTTPS health checks with 3-retry hysteresis (zero steady-state flash wear) and rich Discord webhook notifications.",
            "* **Real-Time WebSocket Dashboard & Swagger API:** Bootstrap 5 dark-mode Web UI with live telemetry, device log drawer, JSON backup/restore, wireless OTA updates, and embedded OpenAPI 3 / Swagger UI (`/api/docs`).",
            "* **Security Hardened:** Physical BOOT button authorization (10-minute inactivity auto-lock), SSRF protection, DNS rebinding defense, and constant-time OTA token rate-limiting.",
            "",
        ])

    lines.extend([
        "### What's Changed",
        "",
    ])
    lines.extend(format_entries(entries))
    return "\n".join(lines)


def generate_full_changelog():
    tags = get_tags()
    lines = [
        "# Changelog",
        "",
        "All notable changes to the ESP32 Service Uptime Monitor project are documented in this file.",
        "",
    ]

    if not tags:
        # No tags released yet, all commits are unreleased
        entries = get_commit_log()
        lines.append("## [Unreleased]")
        lines.append("")
        lines.extend(format_entries(entries))
        lines.append("")
        return "\n".join(lines)

    # Check for unreleased commits on HEAD ahead of the latest tag
    latest_tag = tags[0]
    unreleased_entries = get_commit_log(f"{latest_tag}..HEAD")
    if unreleased_entries:
        lines.append("## [Unreleased]")
        lines.append("")
        lines.extend(format_entries(unreleased_entries))
        lines.append("")

    # Process all tags
    for i, tag in enumerate(tags):
        tag_date = get_tag_date(tag)
        prev_tag = run_git(['describe', '--tags', '--abbrev=0', f"{tag}^"])
        if prev_tag:
            rev_range = f"{prev_tag}..{tag}"
        else:
            rev_range = tag

        entries = get_commit_log(rev_range)
        clean_tag = tag.lstrip("v")
        lines.append(f"## [{clean_tag}] - {tag_date}")
        lines.append("")
        lines.extend(format_entries(entries))
        lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate changelog and release notes from git history.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--release-notes", nargs="?", const="", help="Generate release notes for target tag (default: current tag)")
    group.add_argument("--full", action="store_true", help="Generate complete CHANGELOG.md")

    args = parser.parse_args()

    if args.release_notes is not None:
        target = args.release_notes if args.release_notes != "" else None
        print(generate_release_notes(target))
    elif args.full:
        print(generate_full_changelog())


if __name__ == "__main__":
    main()
