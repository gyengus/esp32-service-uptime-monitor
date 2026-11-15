#include "FileManager.h"

#ifndef DISABLE_FILE_MANAGER

FileManager::FileManager(fs::FS &fs, const String &basePath)
    : _fs(&fs), _basePath(basePath), _logHandler(nullptr), _availabilityCheck(nullptr), _authCheck(nullptr) {
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

void FileManager::setAuthCheck(AuthCheck check) {
    _authCheck = check;
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

static bool isSafeFilename(const String& name) {
    if (name.length() == 0 || name.length() > 64) return false;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

static bool isProtectedSystemFile(const String& name) {
    String lower = name;
    lower.toLowerCase();
    if (lower == "index.html" || lower == "config.html" || lower == "app.js" ||
        lower == "app.config.js" || lower == ".version" || lower == "config.json" ||
        lower == "monitored_entities.json" || lower.endsWith(".bin")) {
        return true;
    }
    return false;
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
    /**
     * @openapi
     * /fileManager:
     *   get:
     *     x-internal: true
     *     tags: [File Manager]
     *     summary: Serve File Manager HTML interface
     *     responses:
     *       '200':
     *         description: File Manager HTML page
     *         content:
     *           text/html:
     *             schema:
     *               type: string
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: Storage not available
     */
    server.on("/fileManager", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (_authCheck && !_authCheck(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }
        String path = buildFullPath("", "fileManager.html");
        request->send(*_fs, path, "text/html");
    });

    /**
     * @openapi
     * /mkdir:
     *   post:
     *     x-internal: true
     *     tags: [File Manager]
     *     summary: Create a directory in storage
     *     parameters:
     *       - in: query
     *         name: dir
     *         required: true
     *         schema:
     *           type: string
     *         description: Directory name to create
     *       - in: query
     *         name: path
     *         required: false
     *         schema:
     *           type: string
     *         description: Parent directory path relative to basePath
     *     responses:
     *       '200':
     *         description: Directory created
     *       '400':
     *         description: Missing dir parameter
     *       '403':
     *         description: Invalid characters in directory name or path
     *       '404':
     *         description: Specified parent path does not exist
     *       '409':
     *         description: Directory already exists
     *       '500':
     *         description: Storage not available or creation failed
     */
    server.on("/mkdir", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_authCheck && !_authCheck(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (request->hasParam("dir")) {
            String dirname = request->getParam("dir")->value();

            if (dirname.indexOf("..") != -1 || dirname.indexOf('/') != -1 || dirname.indexOf('\\') != -1 || !isSafeFilename(dirname)) {
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

    /**
     * @openapi
     * /upload:
     *   post:
     *     x-internal: true
     *     tags: [File Manager]
     *     summary: Upload a file to storage
     *     parameters:
     *       - in: query
     *         name: path
     *         required: false
     *         schema:
     *           type: string
     *         description: Target directory path relative to basePath
     *     requestBody:
     *       required: true
     *       content:
     *         multipart/form-data:
     *           schema:
     *             type: object
     *             required: [file]
     *             properties:
     *               file:
     *                 type: string
     *                 format: binary
     *     responses:
     *       '200':
     *         description: File uploaded (OK)
     *       '403':
     *         description: Invalid filename or path
     *       '500':
     *         description: Storage not available
     */
    server.on("/upload", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_authCheck && !_authCheck(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        request->send(200, "text/plain", "OK");
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (_authCheck && !_authCheck(request)) {
            if (index == 0) {
                request->send(403, "text/plain", "Forbidden");
            }
            return;
        }
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (filename.indexOf("..") != -1 || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1 ||
            !isSafeFilename(filename) || isProtectedSystemFile(filename)) {
            log("error", "Invalid or protected filename for upload");
            request->send(403, "text/plain", "Invalid or protected filename");
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

    /**
     * @openapi
     * /list:
     *   get:
     *     x-internal: true
     *     tags: [File Manager]
     *     summary: List files and subdirectories in a storage directory
     *     parameters:
     *       - in: query
     *         name: path
     *         required: false
     *         schema:
     *           type: string
     *         description: Directory path relative to basePath
     *     responses:
     *       '200':
     *         description: Directory entries
     *         content:
     *           application/json:
     *             schema:
     *               type: array
     *               items:
     *                 $ref: '#/components/schemas/FileEntry'
     *       '403':
     *         description: Invalid path
     *       '500':
     *         description: Storage not available
     */
    server.on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (_authCheck && !_authCheck(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
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

    /**
     * @openapi
     * /delete:
     *   delete:
     *     x-internal: true
     *     tags: [File Manager]
     *     summary: Delete a file or empty directory from storage
     *     parameters:
     *       - in: query
     *         name: file
     *         required: true
     *         schema:
     *           type: string
     *         description: File or directory name
     *       - in: query
     *         name: path
     *         required: false
     *         schema:
     *           type: string
     *         description: Parent directory path relative to basePath
     *     responses:
     *       '200':
     *         description: Item deleted
     *       '400':
     *         description: Missing file parameter
     *       '403':
     *         description: Invalid filename or path
     *       '404':
     *         description: File not found
     *       '500':
     *         description: Storage not available or deletion failed
     */
    server.on("/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (_authCheck && !_authCheck(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        if (!isStorageAvailable() || !_fs) {
            log("error", "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (request->hasParam("file")) {
            String filename = request->getParam("file")->value();

            if (filename.indexOf("..") != -1 || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1 ||
                !isSafeFilename(filename) || isProtectedSystemFile(filename)) {
                request->send(403, "text/plain", "Invalid or protected filename");
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

    /**
     * @openapi
     * /download:
     *   get:
     *     x-internal: true
     *     tags: [File Manager]
     *     summary: Download a file from storage
     *     parameters:
     *       - in: query
     *         name: file
     *         required: true
     *         schema:
     *           type: string
     *         description: File name to download
     *       - in: query
     *         name: path
     *         required: false
     *         schema:
     *           type: string
     *         description: Parent directory path relative to basePath
     *     responses:
     *       '200':
     *         description: Binary file stream
     *         content:
     *           application/octet-stream:
     *             schema:
     *               type: string
     *               format: binary
     *       '400':
     *         description: Missing file parameter
     *       '403':
     *         description: Invalid filename or path
     *       '404':
     *         description: File not found
     *       '500':
     *         description: Storage not available
     */
    server.on("/download", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (_authCheck && !_authCheck(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
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

#endif // DISABLE_FILE_MANAGER
