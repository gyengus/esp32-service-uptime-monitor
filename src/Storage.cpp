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
#include "driver/sdmmc_host.h"
#include <SD_MMC.h>
#include <SPI.h>
#include <FS.h>
#include <Arduino.h>

#include "Config.h"

Storage& Storage::getInstance() {
    static Storage instance;
    return instance;
}

bool Storage::init() {
    if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_DATA_PIN)) {
        Logger.critical(MAINLOG, "SD Card Pin Configuration Failed");
        return false;
    }

    if (!mount()) return false;

    sdmmc_host_t sdmmc_host = SDMMC_HOST_DEFAULT();
    sdmmc_host.flags = SDMMC_HOST_FLAG_1BIT;
    Logger.info(MAINLOG, "SD Card Mounted");

    return true;
}

bool Storage::mount() {
    if (!SD_MMC.begin("/sdcard", true)) {
        Logger.critical(MAINLOG, "SD Card Mount Failed");
        return false;
    }
    return true;
}

bool Storage::unmount() {
    SD_MMC.end();
    Logger.info(MAINLOG, "SD Card Unmounted");
    return true;
}

bool Storage::isAvailable() {
    return SD_MMC.cardType() != CARD_NONE;
}

bool Storage::fileExists(const String &path) {
    return SD_MMC.exists(path);
}

File Storage::getFile(const String &path, const String &mode) {
    return SD_MMC.open(path, mode.c_str());
}

String Storage::readFile(const String &path) {
    File file = SD_MMC.open(path, FILE_READ);
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
    File file = SD_MMC.open(path, FILE_WRITE);
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

    Logger.debug(MAINLOG, "File written successfully: %s, %s", path.c_str(), content.c_str());
    return true;
}

bool Storage::deleteFile(const String &path) {
    if (SD_MMC.remove(path)) {
        Logger.debug(MAINLOG, "File deleted: %s", path.c_str());
        return true;
    } else {
        Logger.error(MAINLOG, "Failed to delete file: %s", path.c_str());
        return false;
    }
}

Storage& storage = Storage::getInstance();

#endif // !UNIT_TEST
