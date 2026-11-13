Import("env")
import os
import re

project_dir = env.get("PROJECT_DIR", ".")
is_release = os.environ.get("RELEASE", "").strip().lower() in ("1", "true", "yes", "on")

def check_flag(flag_name, env_vars, config_h_macro):
    # 1. Release mode enables all disable flags
    if is_release and flag_name.startswith("DISABLE_"):
        return True

    # 2. Check OS environment variables
    for var in env_vars:
        val = os.environ.get(var, "").strip().lower()
        if val in ("1", "true", "yes", "on"):
            return True

    # 3. Check PLATFORMIO_BUILD_FLAGS
    pio_flags = os.environ.get("PLATFORMIO_BUILD_FLAGS", "")
    for var in env_vars:
        if var in pio_flags:
            return True

    # 4. Check include/Config.h
    config_path = os.path.join(project_dir, "include", "Config.h")
    if os.path.isfile(config_path):
        try:
            with open(config_path, "r", encoding="utf-8") as f:
                for line in f:
                    stripped = line.strip()
                    if stripped.startswith("//") or stripped.startswith("/*"):
                        continue
                    if re.match(r'^#define\s+' + re.escape(config_h_macro) + r'\b', stripped):
                        return True
        except Exception:
            pass

    return False

def append_define_if_missing(env, define_name):
    cppdefines = env.get("CPPDEFINES", [])
    already_defined = False
    if isinstance(cppdefines, (list, tuple)):
        for d in cppdefines:
            if (isinstance(d, (list, tuple)) and len(d) > 0 and d[0] == define_name) or d == define_name:
                already_defined = True
                break
    elif define_name in str(cppdefines):
        already_defined = True

    if not already_defined:
        env.Append(CPPDEFINES=[define_name])

# 1. File Manager toggle
if check_flag("DISABLE_FILE_MANAGER", ["DISABLE_FILE_MANAGER"], "DISABLE_FILE_MANAGER"):
    append_define_if_missing(env, "DISABLE_FILE_MANAGER")

# 2. Memory statistics logging toggle
if check_flag("DISABLE_MEM_STATS_LOGGING", ["DISABLE_MEM_STATS_LOGGING", "DISABLE_MEM_STATS"], "DISABLE_MEM_STATS_LOGGING"):
    append_define_if_missing(env, "DISABLE_MEM_STATS_LOGGING")
