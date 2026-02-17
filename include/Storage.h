#ifndef _STORAGE_H_
#define _STORAGE_H_
#include <Arduino.h>
#include <FS.h>

class Storage {
public:
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    static Storage& getInstance();

    bool init();
    bool mount();
    bool unmount();
    bool isAvailable();
    bool fileExists(const String& path);
    File getFile(const String &path, const String &mode);
    String readFile(const String& path);
    bool writeFile(const String& path, const String& content);
    bool deleteFile(const String &path);
private:
    Storage() {};
};

extern Storage& storage;

#endif // _STORAGE_H_
