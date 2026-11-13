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

# 3. Log level configuration
# In Release mode, MIN_LOG_LEVEL is automatically changed from ELOG_LEVEL_DEBUG to ELOG_LEVEL_INFO.
# Can also be set via MIN_LOG_LEVEL=INFO / ELOG_LEVEL_INFO in OS env, Config.h, or platformio.ini.
target_log_level = None

min_log_env = os.environ.get("MIN_LOG_LEVEL", "").strip().upper()
if min_log_env in ("INFO", "ELOG_LEVEL_INFO", "7"):
    target_log_level = "ELOG_LEVEL_INFO"
elif min_log_env in ("DEBUG", "ELOG_LEVEL_DEBUG", "8"):
    target_log_level = "ELOG_LEVEL_DEBUG"
elif is_release:
    target_log_level = "ELOG_LEVEL_INFO"
else:
    # Check PLATFORMIO_BUILD_FLAGS
    pio_flags = os.environ.get("PLATFORMIO_BUILD_FLAGS", "")
    if "MIN_LOG_LEVEL=ELOG_LEVEL_INFO" in pio_flags or "MIN_LOG_LEVEL=INFO" in pio_flags:
        target_log_level = "ELOG_LEVEL_INFO"
    else:
        # Check Config.h
        config_path = os.path.join(project_dir, "include", "Config.h")
        if os.path.isfile(config_path):
            try:
                with open(config_path, "r", encoding="utf-8") as f:
                    for line in f:
                        stripped = line.strip()
                        if stripped.startswith("//") or stripped.startswith("/*"):
                            continue
                        m = re.match(r'^#define\s+MIN_LOG_LEVEL\s+(\S+)', stripped)
                        if m:
                            val = m.group(1).upper()
                            if val in ("ELOG_LEVEL_INFO", "INFO", "7"):
                                target_log_level = "ELOG_LEVEL_INFO"
                            elif val in ("ELOG_LEVEL_DEBUG", "DEBUG", "8"):
                                target_log_level = "ELOG_LEVEL_DEBUG"
                            break
            except Exception:
                pass

if target_log_level:
    # Update in BUILD_FLAGS if present
    build_flags = env.get("BUILD_FLAGS", [])
    found_in_build_flags = False
    for i, flag in enumerate(build_flags):
        if re.search(r'-D\s*MIN_LOG_LEVEL\b', flag):
            build_flags[i] = f"-D MIN_LOG_LEVEL={target_log_level}"
            found_in_build_flags = True

    # Update in CPPDEFINES if present
    cppdefines = env.get("CPPDEFINES", [])
    found_in_cppdefines = False
    if isinstance(cppdefines, (list, tuple)):
        for i, d in enumerate(cppdefines):
            if isinstance(d, (list, tuple)) and len(d) > 0 and d[0] == "MIN_LOG_LEVEL":
                cppdefines[i] = ("MIN_LOG_LEVEL", target_log_level)
                found_in_cppdefines = True
            elif isinstance(d, str) and d.startswith("MIN_LOG_LEVEL="):
                cppdefines[i] = f"MIN_LOG_LEVEL={target_log_level}"
                found_in_cppdefines = True

    if not found_in_build_flags and not found_in_cppdefines:
        env.Append(CPPDEFINES=[("MIN_LOG_LEVEL", target_log_level)])
