#include "fileManager.h"
#include "Config.h"
#include <Elog.h>
#include <SD_MMC.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "Storage.h"

void registerFileHandlers(AsyncWebServer &server) {

    server.on("/fileManager", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SD_MMC, (String(WEB_UI_PATH) + "/fileManager.html"), "text/html");
    });
    
    server.on("/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "SD Card not available");
            request->send(500, "text/plain", "SD Card not available");
            return;
        }
        if (request->hasParam("dir")) {
            String dirname = request->getParam("dir")->value();
            
            // Biztonsági ellenőrzés
            if (dirname.indexOf("..") != -1 || dirname.indexOf("/") != -1) {
                request->send(403, "text/plain", "Ervenytelen karakterek a nevben!");
                return;
            }

            if (request->hasParam("path")) {
                String path = request->getParam("path")->value();
                if (path.indexOf("..") != -1) {
                    request->send(403, "text/plain", "Ervenytelen karakterek a path-ben!");
                    return;
                }
                path = String(WEB_UI_PATH) + path;
                if (!SD_MMC.exists(path)) {
                    request->send(404, "text/plain", "A megadott path nem letezik");
                    return;
                }
                dirname = path + "/" + dirname;
            } else {
                dirname = String(WEB_UI_PATH) + "/" + dirname;
            }

            if (SD_MMC.exists(dirname)) {
                request->send(409, "text/plain", "A mappa mar letezik");
            } else {
                if (SD_MMC.mkdir(dirname)) {
                    Logger.debug(MAINLOG, "Mappa letrehozva: %s", dirname.c_str());
                    request->send(200, "text/plain", "Mappa letrehozva");
                } else {
                    request->send(500, "text/plain", "Hiba a letrehozasnal");
                }
            }
        } else {
            request->send(400, "text/plain", "Hianyzo 'dir' parameter");
        }
    });

    // --- FELTÖLTÉS KEZELÉSE ---
    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "OK");
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "SD Card not available");
            request->send(500, "text/plain", "SD Card not available");
            return;
        }        

        if (!index) {
            String uploadPath = WEB_UI_PATH;
            if (request->hasParam("path")) uploadPath += request->getParam("path")->value();
            String path = uploadPath + "/" + filename;
            Logger.debug(MAINLOG, "Feltoltes: %s", path.c_str());
            // Fájl megnyitása írásra az SD_MMC-n
            request->_tempFile = SD_MMC.open(path, FILE_WRITE);
        }
        
        if (request->_tempFile) {
            if (len) {
                request->_tempFile.write(data, len);
            }
            if (final) {
                request->_tempFile.close();
                Logger.debug(MAINLOG, "Sikeres mentés.");
            }
        }
    });

    // --- LISTÁZÁS KEZELÉSE ---
    server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "SD Card not available");
            request->send(500, "text/plain", "SD Card not available");
            return;
        }
        String folderPath = WEB_UI_PATH;
        if (request->hasParam("path")) {
            folderPath += request->getParam("path")->value();
        }
        File dir = SD_MMC.open(folderPath);
        JsonDocument doc;
        JsonArray array = doc.to<JsonArray>();
        if (!dir || !dir.isDirectory()) {
            SD_MMC.mkdir(folderPath);
            dir = SD_MMC.open(folderPath);
        }
        File file = dir.openNextFile();
        while (file) {
            JsonObject obj = array.add<JsonObject>();
            obj["name"] = String(file.name());
            obj["isDir"] = file.isDirectory();
            obj["size"] = file.size();
            file = dir.openNextFile();
        }
        dir.close();
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    // --- TÖRLÉS ---
    server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "SD Card not available");
            request->send(500, "text/plain", "SD Card not available");
            return;
        }

        if (request->hasParam("file")) {
            String filename = request->getParam("file")->value();
            
            if (filename.indexOf("..") != -1) {
                request->send(403, "text/plain", "Invalid path");
                return;
            }

            String fullPath = String(WEB_UI_PATH);
            if (request->hasParam("path", false)) fullPath += request->getParam("path", false)->value();
            fullPath += "/" + filename;
            Logger.debug(MAINLOG, "Deleting: %s", fullPath.c_str());

            if (SD_MMC.exists(fullPath)) {
                if (SD_MMC.remove(fullPath)) {
                    Logger.debug(MAINLOG, "Deleted: %s", fullPath.c_str());
                    request->send(200, "text/plain", "Sikeres torles");
                } else {
                    Logger.error(MAINLOG, "Error when deleting file: %s", fullPath.c_str());
                    request->send(500, "text/plain", "Hiba a torles soran");
                }
            } else {
                request->send(404, "text/plain", "File not found");
            }
        } else {
            request->send(400, "text/plain", "Missing 'file' parameter");
        }
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "SD Card not available");
            request->send(500, "text/plain", "SD Card not available");
            return;
        }

        if (request->hasParam("file") && request->hasParam("path")) {
            String fileName = request->getParam("file")->value();
            String path = request->getParam("path")->value();
            String fullPath = String(WEB_UI_PATH) + path + "/" + fileName;
            Logger.debug(MAINLOG, "Downloading: %s", fullPath.c_str());

            if (SD_MMC.exists(fullPath)) {
                AsyncWebServerResponse *response = request->beginResponse(SD_MMC, fullPath, "application/octet-stream");
                response->addHeader("Content-Disposition", "attachment; filename=" + fileName);
                request->send(response);
            } else {
                request->send(404, "text/plain", "File Not Found");
            }
        } else {
            request->send(400, "text/plain", "Missing parameters");
        }
    });
}
