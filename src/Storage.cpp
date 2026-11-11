#include "Storage.h"

#ifdef UNIT_TEST

Storage& Storage::getInstance() {
    static Storage instance;
    return instance;
}

Storage& storage = Storage::getInstance();

bool Storage::init() { return true; }
bool Storage::mount() { return true; }
bool Storage::unmount() { return true; }
bool Storage::isAvailable() { return true; }

StorageStats Storage::getStats() {
    uint64_t used = 0;
    for (const auto& kv : files) {
        used += kv.second.size();
    }
    const uint64_t total = 1048576ULL;
    return {
        "MockFS",
        true,
        "",
        0,
        total,
        used,
        used < total ? (total - used) : 0
    };
}

bool Storage::fileExists(const String& path) {
    return files.find(path) != files.end();
}

File Storage::getFile(const String &path, const String &mode) {
    if (mode == "r" || mode == FILE_READ) {
        auto it = files.find(path);
        if (it != files.end()) {
            return File(path, mode, &(it->second));
        }
        return File();
    } else {
        return File(path, mode, &files[path]);
    }
}

String Storage::readFile(const String& path) {
    auto it = files.find(path);
    if (it != files.end()) return String(it->second.c_str());
    return String();
}

bool Storage::writeFile(const String& path, const String& content) {
    files[path] = content.c_str();
    return true;
}

bool Storage::deleteFile(const String& path) {
    return files.erase(path) > 0;
}

#else

#include <Elog.h>
#ifdef USE_LITTLEFS
#include <LittleFS.h>
#else
#include "driver/sdmmc_host.h"
#include <SD_MMC.h>
#include <SPI.h>
#endif
#include <FS.h>
#include <Arduino.h>

#include "Config.h"
#include "globals.h"

Storage& Storage::getInstance() {
    static Storage instance;
    return instance;
}

fs::FS& Storage::getFS() {
#ifdef USE_LITTLEFS
    return LittleFS;
#else
    return SD_MMC;
#endif
}

bool Storage::init() {
#ifdef USE_LITTLEFS
    if (!LittleFS.begin(false)) {
        Logger.warning(MAINLOG, "LittleFS Mount Failed, formatting flash partition...");
        disableLoopWDT();
        bool formatted = LittleFS.format();
        enableLoopWDT();
        if (!formatted || !LittleFS.begin(false)) {
            Logger.critical(MAINLOG, "LittleFS Format or Mount Failed");
            return false;
        }
        Logger.info(MAINLOG, "LittleFS formatted and mounted successfully");
    }
    Logger.info(MAINLOG, "LittleFS Mounted (Total: %u KB, Used: %u KB)",
                (unsigned int)(LittleFS.totalBytes() / 1024),
                (unsigned int)(LittleFS.usedBytes() / 1024));
    return true;
#else
    if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_DATA_PIN)) {
        Logger.critical(MAINLOG, "SD Card Pin Configuration Failed");
        return false;
    }

    if (!mount()) return false;

    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT();
    sdmmc_host.flags = SDMMC_HOST_FLAG_1BIT;
    Logger.info(MAINLOG, "SD Card Mounted");

    return true;
#endif
}

bool Storage::mount() {
#ifdef USE_LITTLEFS
    return LittleFS.begin(false);
#else
    if (!SD_MMC.begin("/sdcard", true)) {
        Logger.critical(MAINLOG, "SD Card Mount Failed");
        return false;
    }
    return true;
#endif
}

bool Storage::unmount() {
#ifdef USE_LITTLEFS
    LittleFS.end();
    Logger.info(MAINLOG, "LittleFS Unmounted");
    return true;
#else
    SD_MMC.end();
    Logger.info(MAINLOG, "SD Card Unmounted");
    return true;
#endif
}

bool Storage::isAvailable() {
#ifdef USE_LITTLEFS
    return LittleFS.totalBytes() > 0;
#else
    return SD_MMC.cardType() != CARD_NONE;
#endif
}

StorageStats Storage::getStats() {
    StorageStats stats{};
#ifdef USE_LITTLEFS
    stats.backend = "LittleFS";
    stats.cardType = "";
    stats.cardSizeBytes = 0;
    if (isOtaUpdating) {
        stats.available = false;
        return stats;
    }
    stats.totalBytes = LittleFS.totalBytes();
    stats.usedBytes = LittleFS.usedBytes();
    stats.available = stats.totalBytes > 0;
    stats.freeBytes = (stats.totalBytes >= stats.usedBytes) ? (stats.totalBytes - stats.usedBytes) : 0;
#else
    stats.backend = "SD_MMC";
    if (isOtaUpdating) {
        stats.available = false;
        return stats;
    }
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        stats.available = false;
        stats.cardType = "NONE";
        return stats;
    }
    stats.available = true;
    switch (cardType) {
        case CARD_MMC: stats.cardType = "MMC"; break;
        case CARD_SD: stats.cardType = "SDSC"; break;
        case CARD_SDHC: stats.cardType = "SDHC"; break;
        default: stats.cardType = "UNKNOWN"; break;
    }
    stats.cardSizeBytes = SD_MMC.cardSize();
    stats.totalBytes = SD_MMC.totalBytes();
    stats.usedBytes = SD_MMC.usedBytes();
    stats.freeBytes = (stats.totalBytes >= stats.usedBytes) ? (stats.totalBytes - stats.usedBytes) : 0;
#endif
    return stats;
}

bool Storage::fileExists(const String &path) {
    return getFS().exists(path);
}

File Storage::getFile(const String &path, const String &mode) {
    return getFS().open(path, mode.c_str());
}

String Storage::readFile(const String &path) {
    File file = getFS().open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        Logger.error(MAINLOG, "Failed to open file for reading: %s", path.c_str());
        return String();
    }

    String content;
    while (file.available()) {
        content += char(file.read());
    }
    file.close();
    return content;
}

bool Storage::writeFile(const String &path, const String &content) {
    File file = getFS().open(path, FILE_WRITE);
    if (!file) {
        Logger.error(MAINLOG, "Failed to open file for writing: %s", path.c_str());
        return false;
    }

    size_t written = file.print(content);
    file.close();
    if (written != content.length() ) {
        Logger.error(MAINLOG, "Failed to write to file: %s", path.c_str());
        return false;
    }

    Logger.debug(MAINLOG, "File written successfully: %s (%u bytes)", path.c_str(), (unsigned int)content.length());
    return true;
}

bool Storage::deleteFile(const String &path) {
    if (getFS().remove(path)) {
        Logger.debug(MAINLOG, "File deleted: %s", path.c_str());
        return true;
    } else {
        Logger.error(MAINLOG, "Failed to delete file: %s", path.c_str());
        return false;
    }
}

Storage& storage = Storage::getInstance();

#endif // !UNIT_TEST
