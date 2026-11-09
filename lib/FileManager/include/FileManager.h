#ifndef _FILE_MANAGER_H_
#define _FILE_MANAGER_H_

#include <Arduino.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>

class FileManager {
public:
    using LogHandler = std::function<void(const char* level, const char* msg)>;
    using AvailabilityCheck = std::function<bool()>;

    explicit FileManager(fs::FS &fs, const String &basePath = "/web");

    void setFileSystem(fs::FS &fs);
    void setBasePath(const String &basePath);
    void setLogger(LogHandler handler);
    void setAvailabilityCheck(AvailabilityCheck check);

    void registerHandlers(AsyncWebServer &server);

    const String& getBasePath() const { return _basePath; }

private:
    fs::FS *_fs;
    String _basePath;
    LogHandler _logHandler;
    AvailabilityCheck _availabilityCheck;

    void log(const char* level, const char* msg);
    String buildFullPath(const String &pathParam, const String &fileName = "") const;
    bool isStorageAvailable() const;
};

void registerFileHandlers(AsyncWebServer &server,
                          fs::FS &fs,
                          const String &basePath = "/web",
                          FileManager::AvailabilityCheck check = nullptr,
                          FileManager::LogHandler logHandler = nullptr);

#endif // _FILE_MANAGER_H_
