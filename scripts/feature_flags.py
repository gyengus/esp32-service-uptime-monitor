Import("env")
import os
import re

project_dir = env.get("PROJECT_DIR", ".")

# Check if DISABLE_FILE_MANAGER is requested via OS environment or Config.h
disable_fm_env = os.environ.get("DISABLE_FILE_MANAGER", "").strip().lower()
is_disabled = disable_fm_env in ("1", "true", "yes", "on")

if not is_disabled:
    pio_flags = os.environ.get("PLATFORMIO_BUILD_FLAGS", "")
    if "DISABLE_FILE_MANAGER" in pio_flags:
        is_disabled = True

if not is_disabled:
    config_path = os.path.join(project_dir, "include", "Config.h")
    if os.path.isfile(config_path):
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                for line in f:
                    stripped = line.strip()
                    if stripped.startswith("//") or stripped.startswith("/*"):
                        continue
                    if re.match(r'^#define\s+DISABLE_FILE_MANAGER\b', stripped):
                        is_disabled = True
                        break
        except Exception:
            pass

if is_disabled:
    cppdefines = env.get("CPPDEFINES", [])
    already_defined = False
    if isinstance(cppdefines, (list, tuple)):
        for d in cppdefines:
            if (isinstance(d, (list, tuple)) and len(d) > 0 and d[0] == "DISABLE_FILE_MANAGER") or d == "DISABLE_FILE_MANAGER":
                already_defined = True
                break
    elif "DISABLE_FILE_MANAGER" in str(cppdefines):
        already_defined = True

    if not already_defined:
        env.Append(CPPDEFINES=["DISABLE_FILE_MANAGER"])
