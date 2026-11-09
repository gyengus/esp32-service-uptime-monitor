#include "FileManager.h"

FileManager::FileManager(fs::FS &fs, const String &basePath)
    : _fs(&fs), _basePath(basePath), _logHandler(nullptr), _availabilityCheck(nullptr) {
}

void FileManager::setFileSystem(fs::FS &fs) {
    _fs = &fs;
}

void FileManager::setBasePath(const String &basePath) {
    _basePath = basePath;
}

void FileManager::setLogger(LogHandler handler) {
    _logHandler = handler;
}

void FileManager::setAvailabilityCheck(AvailabilityCheck check) {
    _availabilityCheck = check;
}

bool FileManager::isStorageAvailable() const {
    if (_availabilityCheck) {
        return _availabilityCheck();
    }
    return _fs != nullptr;
}

void FileManager::log(const char* level, const char* msg) {
    if (_logHandler) {
        _logHandler(level, msg);
    }
#if defined(ARDUINO) && !defined(UNIT_TEST)
    else {
        if (strcmp(level, "error") == 0) {
            log_e("[FileManager] %s", msg);
        } else if (strcmp(level, "warning") == 0) {
            log_w("[FileManager] %s", msg);
        } else if (strcmp(level, "info") == 0) {
            log_i("[FileManager] %s", msg);
        } else {
            log_d("[FileManager] %s", msg);
        }
    }
#endif
}

String FileManager::buildFullPath(const String &pathParam, const String &fileName) const {
    String fullPath = _basePath;
    if (fullPath.endsWith("/")) {
        fullPath.remove(fullPath.length() - 1);
    }

    if (pathParam.length() > 0) {
        if (!pathParam.startsWith("/")) {
            fullPath += "/";
        }
        fullPath += pathParam;
        if (fullPath.endsWith("/")) {
            fullPath.remove(fullPath.length() - 1);
        }
    }

    if (fileName.length() > 0) {
        fullPath += "/" + fileName;
    }

    if (fullPath.length() == 0) {
        fullPath = "/";
    }

    return fullPath;
}

void FileManager::registerHandlers(AsyncWebServer &server) {
    server.on("/fileManager", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }
        String path = buildFullPath("", "fileManager.html");
        request->send(*_fs, path, "text/html");
    });

    server.on("/mkdir", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (request->hasParam("dir")) {
            String dirname = request->getParam("dir")->value();

            if (dirname.indexOf("..") != -1 || dirname.indexOf('/') != -1 || dirname.indexOf('\\') != -1) {
                request->send(403, "text/plain", "Invalid characters in directory name");
                return;
            }

            String pathParam = "";
            if (request->hasParam("path")) {
                pathParam = request->getParam("path")->value();
                if (pathParam.indexOf("..") != -1) {
                    request->send(403, "text/plain", "Invalid characters in path");
                    return;
                }
                String parentPath = buildFullPath(pathParam);
                if (!_fs->exists(parentPath)) {
                    request->send(404, "text/plain", "Specified path does not exist");
                    return;
                }
            }

            String fullDirName = buildFullPath(pathParam, dirname);

            if (_fs->exists(fullDirName)) {
                request->send(409, "text/plain", "Directory already exists");
            } else {
                if (_fs->mkdir(fullDirName)) {
                    log("debug", ("Directory created: " + fullDirName).c_str());
                    request->send(200, "text/plain", "Directory created");
                } else {
                    log("error", ("Failed to create directory: " + fullDirName).c_str());
                    request->send(500, "text/plain", "Failed to create directory");
                }
            }
        } else {
            request->send(400, "text/plain", "Missing 'dir' parameter");
        }
    });

    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "OK");
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (filename.indexOf("..") != -1 || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1) {
            log("error", "Invalid filename for upload");
            request->send(403, "text/plain", "Invalid filename");
            return;
        }

        String pathParam = "";
        if (request->hasParam("path")) {
            pathParam = request->getParam("path")->value();
            if (pathParam.indexOf("..") != -1) {
                log("error", "Invalid path for upload");
                request->send(403, "text/plain", "Invalid path");
                return;
            }
        }

        if (!index) {
            String path = buildFullPath(pathParam, filename);
            log("debug", ("Uploading: " + path).c_str());
            request->_tempFile = _fs->open(path, FILE_WRITE);
            if (!request->_tempFile) {
                log("error", ("Failed to open file for write: " + path).c_str());
            }
        }

        if (request->_tempFile) {
            if (len) {
                request->_tempFile.write(data, len);
            }
            if (final) {
                request->_tempFile.close();
                log("debug", "File upload completed.");
            }
        }
    });

    server.on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        String pathParam = "";
        if (request->hasParam("path")) {
            pathParam = request->getParam("path")->value();
            if (pathParam.indexOf("..") != -1) {
                request->send(403, "text/plain", "Invalid path");
                return;
            }
        }

        String folderPath = buildFullPath(pathParam);
        File dir = _fs->open(folderPath);
        JsonDocument doc;
        JsonArray array = doc.to<JsonArray>();

        if (!dir || !dir.isDirectory()) {
            _fs->mkdir(folderPath);
            dir = _fs->open(folderPath);
        }

        if (dir && dir.isDirectory()) {
            File file = dir.openNextFile();
            while (file) {
                JsonObject obj = array.add<JsonObject>();
                const char* rawName = file.name();
                const char* slash = strrchr(rawName, '/');
                const char* displayName = (slash && *(slash + 1)) ? slash + 1 : rawName;
                obj["name"] = String(displayName);
                obj["isDir"] = file.isDirectory();
                obj["size"] = file.size();
                file = dir.openNextFile();
            }
            dir.close();
        }

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    server.on("/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (request->hasParam("file")) {
            String filename = request->getParam("file")->value();

            if (filename.indexOf("..") != -1 || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1) {
                request->send(403, "text/plain", "Invalid filename");
                return;
            }

            String pathParam = "";
            if (request->hasParam("path", false)) {
                pathParam = request->getParam("path", false)->value();
                if (pathParam.indexOf("..") != -1) {
                    request->send(403, "text/plain", "Invalid path");
                    return;
                }
            }

            String fullPath = buildFullPath(pathParam, filename);
            log("debug", ("Deleting: " + fullPath).c_str());

            if (_fs->exists(fullPath)) {
                File item = _fs->open(fullPath);
                bool isDir = item && item.isDirectory();
                if (item) {
                    item.close();
                }

                bool removed = isDir ? _fs->rmdir(fullPath) : _fs->remove(fullPath);
                if (removed) {
                    log("debug", ("Deleted: " + fullPath).c_str());
                    request->send(200, "text/plain", "Item deleted");
                } else {
                    log("error", ("Error when deleting: " + fullPath).c_str());
                    request->send(500, "text/plain", "Failed to delete item");
                }
            } else {
                request->send(404, "text/plain", "File not found");
            }
        } else {
            request->send(400, "text/plain", "Missing 'file' parameter");
        }
    });

    server.on("/download", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (request->hasParam("file")) {
            String fileName = request->getParam("file")->value();
            String pathParam = request->hasParam("path") ? request->getParam("path")->value() : "";

            if (fileName.indexOf("..") != -1 || fileName.indexOf('/') != -1 || fileName.indexOf('\\') != -1) {
                request->send(403, "text/plain", "Invalid filename");
                return;
            }
            if (pathParam.indexOf("..") != -1) {
                request->send(403, "text/plain", "Invalid path");
                return;
            }

            String fullPath = buildFullPath(pathParam, fileName);
            log("debug", ("Downloading: " + fullPath).c_str());

            if (_fs->exists(fullPath)) {
                AsyncWebServerResponse *response = request->beginResponse(*_fs, fullPath, "application/octet-stream");
                response->addHeader("Content-Disposition", "attachment; filename=" + fileName);
                request->send(response);
            } else {
                request->send(404, "text/plain", "File Not Found");
            }
        } else {
            request->send(400, "text/plain", "Missing 'file' parameter");
        }
    });
}

void registerFileHandlers(AsyncWebServer &server,
                          fs::FS &fs,
                          const String &basePath,
                          FileManager::AvailabilityCheck check,
                          FileManager::LogHandler logHandler) {
    static FileManager fileManager(fs, basePath);
    fileManager.setFileSystem(fs);
    fileManager.setBasePath(basePath);
    if (check) {
        fileManager.setAvailabilityCheck(check);
    }
    if (logHandler) {
        fileManager.setLogger(logHandler);
    }
    fileManager.registerHandlers(server);
}
