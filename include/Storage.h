#ifndef _STORAGE_H_
#define _STORAGE_H_
#include <Arduino.h>

#ifndef UNIT_TEST
#include <FS.h>
#else
#include <map>
#endif

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
#ifndef UNIT_TEST
    File getFile(const String &path, const String &mode);
#endif
    String readFile(const String& path);
    bool writeFile(const String& path, const String& content);
    bool deleteFile(const String &path);

#ifdef UNIT_TEST
    void clear() { files.clear(); }
    std::map<String, String> files;
#endif

private:
    Storage() {};
};

extern Storage& storage;

#endif // _STORAGE_H_
