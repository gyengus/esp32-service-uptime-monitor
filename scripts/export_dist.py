Import("env")
import os
import shutil
import subprocess
import json

def get_chip_info(env):
    board_mcu = str(env.get("BOARD_MCU", "")).lower()
    if not board_mcu:
        try:
            board_mcu = str(env.BoardConfig().get("build.mcu", "esp32")).lower()
        except Exception:
            board_mcu = "esp32"

    chip_map = {
        "esp32": "esp32",
        "esp32s2": "esp32s2",
        "esp32s3": "esp32s3",
        "esp32c3": "esp32c3",
        "esp32c6": "esp32c6",
        "esp32c5": "esp32c5",
    }
    esptool_chip = chip_map.get(board_mcu, "esp32")

    family_map = {
        "esp32": "ESP32",
        "esp32s2": "ESP32-S2",
        "esp32s3": "ESP32-S3",
        "esp32c3": "ESP32-C3",
        "esp32c6": "ESP32-C6",
        "esp32c5": "ESP32-C5",
    }
    chip_family = family_map.get(esptool_chip, esptool_chip.upper())
    bootloader_offset = "0x1000" if esptool_chip == "esp32" else "0x0000"

    return esptool_chip, chip_family, bootloader_offset

def update_web_flasher_manifest(dist_dir, pioenv, chip_family):
    manifest_path = os.path.join(dist_dir, "manifest.json")
    manifest = {
        "name": "ESP32 Service Uptime Monitor",
        "new_install_prompt_erase": True,
        "builds": []
    }
    if os.path.isfile(manifest_path):
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        except Exception:
            pass

    builds = [b for b in manifest.get("builds", []) if b.get("parts", [{}])[0].get("path") != f"factory-{pioenv}.bin"]
    builds.append({
        "chipFamily": chip_family,
        "parts": [
            { "path": f"factory-{pioenv}.bin", "offset": 0 }
        ]
    })
    manifest["builds"] = builds

    try:
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
    except Exception:
        pass

def export_env_dist(env, pioenv):
    if not pioenv or pioenv.endswith("-ota") or pioenv == "native":
        return

    project_dir = env.get("PROJECT_DIR", ".")
    build_dir = env.subst("$BUILD_DIR")
    dist_dir = os.path.join(project_dir, "dist")
    target_dist_dir = os.path.join(dist_dir, pioenv)
    os.makedirs(target_dist_dir, exist_ok=True)

    firmware_bin = os.path.join(build_dir, "firmware.bin")
    bootloader_bin = os.path.join(build_dir, "bootloader.bin")
    partitions_bin = os.path.join(build_dir, "partitions.bin")

    if not os.path.isfile(firmware_bin):
        return

    # Export standalone firmware binary
    dest_firmware = os.path.join(dist_dir, f"firmware-{pioenv}.bin")
    shutil.copy2(firmware_bin, dest_firmware)
    shutil.copy2(firmware_bin, os.path.join(target_dist_dir, "firmware.bin"))
    print(f"\n[DIST] Exported firmware: {dest_firmware}")

    # Copy bootloader & partitions if present
    if os.path.isfile(bootloader_bin):
        shutil.copy2(bootloader_bin, os.path.join(target_dist_dir, "bootloader.bin"))
    if os.path.isfile(partitions_bin):
        shutil.copy2(partitions_bin, os.path.join(target_dist_dir, "partitions.bin"))

    # Locate boot_app0.bin
    boot_app0_bin = None
    try:
        framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
        if framework_dir:
            cand = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
            if os.path.isfile(cand):
                boot_app0_bin = cand
    except Exception:
        pass

    if not boot_app0_bin:
        home = os.path.expanduser("~")
        cand = os.path.join(home, ".platformio", "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
        if os.path.isfile(cand):
            boot_app0_bin = cand

    if boot_app0_bin:
        shutil.copy2(boot_app0_bin, os.path.join(target_dist_dir, "boot_app0.bin"))

    esptool_chip, chip_family, bootloader_offset = get_chip_info(env)
    partitions_offset = "0x8000"
    boot_app0_offset = "0xe000"
    firmware_offset = "0x10000"

    flash_size = "4MB"
    try:
        flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    except Exception:
        pass

    flash_mode = env.get("BOARD_FLASH_MODE", "dio")
    flash_freq = "80m"
    try:
        f_flash = str(env.BoardConfig().get("build.f_flash", "80000000L"))
        flash_freq = "80m" if "80" in f_flash else "40m"
    except Exception:
        pass

    # Create merged factory image for ESP Web Tools
    if os.path.isfile(bootloader_bin) and os.path.isfile(partitions_bin) and boot_app0_bin:
        dest_factory = os.path.join(dist_dir, f"factory-{pioenv}.bin")
        target_factory = os.path.join(target_dist_dir, "factory.bin")
        python_exe = env.subst("$PYTHONEXE")

        merge_cmd = [
            python_exe, "-m", "esptool",
            "--chip", esptool_chip,
            "merge-bin",
            "-o", dest_factory,
            "--flash-mode", str(flash_mode).lower(),
            "--flash-freq", flash_freq,
            "--flash-size", str(flash_size),
            bootloader_offset, bootloader_bin,
            partitions_offset, partitions_bin,
            boot_app0_offset, boot_app0_bin,
            firmware_offset, firmware_bin
        ]

        try:
            subprocess.run(merge_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
            shutil.copy2(dest_factory, target_factory)
            print(f"[DIST] Created Web Flasher factory image (merged @ 0x0): {dest_factory}")
            update_web_flasher_manifest(dist_dir, pioenv, chip_family)
            print(f"[DIST] Updated ESP Web Tools manifest: {os.path.join(dist_dir, 'manifest.json')}\n")
        except Exception as e:
            print(f"[DIST] Warning: Could not create merged factory image: {e}")

def export_dist_action(source, target, env):
    pioenv = env.get("PIOENV", "")
    export_env_dist(env, pioenv)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_dist_action)

env.AddCustomTarget(
    name="dist",
    dependencies=["$BUILD_DIR/${PROGNAME}.bin"],
    actions=[export_dist_action],
    title="Export dist binaries",
    description="Export firmware.bin and merged factory.bin for Web Flasher to dist/"
)
