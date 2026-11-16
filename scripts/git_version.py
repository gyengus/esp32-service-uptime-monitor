import os
import re
import subprocess
import datetime


def run_git(args):
    try:
        return subprocess.check_output(['git'] + args, stderr=subprocess.DEVNULL).decode('utf-8').strip()
    except Exception:
        return ""


def get_firmware_version():
    # 1. Explicit CI tag environment variables
    ci_tag = os.environ.get("CI_COMMIT_TAG") or (
        os.environ.get("GITHUB_REF_NAME") if os.environ.get("GITHUB_REF_TYPE") == "tag" else None
    )
    if ci_tag:
        return ci_tag.lstrip("v")

    # 2. Check if git HEAD is on an exact tag
    exact_tag = run_git(['describe', '--tags', '--exact-match'])
    if exact_tag:
        return exact_tag.lstrip("v")

    # 3. Check for most recent tag to calculate next SNAPSHOT
    latest_tag = run_git(['describe', '--tags', '--abbrev=0'])
    if latest_tag:
        clean_tag = latest_tag.lstrip("v")
        m = re.match(r"^(\d+)\.(\d+)\.(\d+)", clean_tag)
        if m:
            major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
            return f"{major}.{minor}.{patch + 1}-SNAPSHOT"

    # 4. Fallback if no tags exist
    return "0.0.1-SNAPSHOT"


commit_hash = run_git(['rev-parse', '--short', 'HEAD']) or "unknown"
build_date = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
firmware_version = get_firmware_version()

try:
    Import("env")
    env.Append(CPPDEFINES=[
        ("FIRMWARE_VERSION", f'\\"{firmware_version}\\"'),
        ("COMMIT_HASH", f'\\"{commit_hash}\\"'),
        ("BUILD_TIMESTAMP", f'\\"{build_date}\\"')
    ])
except Exception:
    pass

if __name__ == "__main__":
    print(f"Firmware Version: {firmware_version}")
    print(f"Commit Hash: {commit_hash}")
    print(f"Build Timestamp: {build_date}")
