#ifndef _STORAGE_H_
#define _STORAGE_H_
#include <Arduino.h>
#include <FS.h>
#include "devices.h"

#ifdef UNIT_TEST
#include <map>
#include <string>
#endif

struct StorageStats {
    String backend;
    bool available;
    String cardType;
    uint64_t cardSizeBytes;
    uint64_t totalBytes;
    uint64_t usedBytes;
    uint64_t freeBytes;
};

class Storage {
public:
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    static Storage& getInstance();

    bool init();
    bool mount();
    bool unmount();
    bool isAvailable();
    StorageStats getStats();
    bool fileExists(const String& path);
    File getFile(const String &path, const String &mode);
    String readFile(const String& path);
    bool writeFile(const String& path, const String& content);
    bool deleteFile(const String &path);

#ifdef UNIT_TEST
    fs::FS& getFS() { return mockFS; }
    void clear() { files.clear(); }
    std::map<String, std::string> files;
#else
    fs::FS& getFS();
#endif

private:
    Storage() {};
};

extern Storage& storage;

#endif // _STORAGE_H_
