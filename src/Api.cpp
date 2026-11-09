#include "Api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Elog.h>
#include <WiFi.h>
#include "driver/temperature_sensor.h"

#include "version.h"
#include "globals.h"
#include "Config.h"
#include "Storage.h"
#include "NetworkContext.h"
#include "MonitoredEntityRepository.h"
#include "MonitoringService.h"
#include "NotificationService.h"
#include "PSRAMAllocator.h"

static PSRAMAllocator psramAllocator;

Api::Api(AsyncWebServer &server) : server(server) {}

void Api::registerEndpoints() {
    registerCommonEndpoints();
    registerConfigEndpoints();
    if (!isAPMode) registerServicesEndpoints();
    registerSystemInformationEndpoints();
}

void Api::registerConfigEndpoints() {
    server.on("/api/config/backup", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        String configJson = storage.readFile(CONFIG_FILE_PATH);
        String entitiesJson = storage.readFile(ENTITIES_FILE_PATH);

        if (configJson.isEmpty() || entitiesJson.isEmpty()) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to read config files\"}");
            return;
        }

        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        DeserializationError configErr = deserializeJson(root["config"], configJson);
        DeserializationError entitiesErr = deserializeJson(root["entities"], entitiesJson);

        if (configErr || entitiesErr) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to parse config files\"}");
            return;
        }

        String backupJson;
        serializeJson(doc, backupJson);

        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->print(backupJson);
        request->send(response);
    });

    server.on("/api/config/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "SD Card not available");
            request->send(500, "text/plain", "SD Card not available");
            return;
        }

        if (index == 0) request->_tempObject = new String("");

        String* body = (String*)request->_tempObject;
        if (body) {
            body->concat((char*)data, len);
        }

        if (final && body) {
            JsonDocument doc(&psramAllocator);
            DeserializationError error = deserializeJson(doc, *body);

            if (error) {
                Logger.error(MAINLOG, "Restore failed: JSON parse error: %s", error.c_str());
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                delete body; request->_tempObject = nullptr;
                return;
            }

            JsonObject root = doc.as<JsonObject>();

            if (!root.containsKey("config") || !root.containsKey("entities")) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing config or entities\"}");
                delete body; request->_tempObject = nullptr;
                return;
            }

            String configJson;
            String entitiesJson;

            serializeJson(root["config"], configJson);
            serializeJson(root["entities"], entitiesJson);

            bool configSaved = storage.writeFile(CONFIG_FILE_PATH, configJson);
            bool entitiesSaved = storage.writeFile(ENTITIES_FILE_PATH, entitiesJson);

            delete body; request->_tempObject = nullptr;

            if (configSaved && entitiesSaved) {
                Logger.info(MAINLOG, "Restore completed successfully");
                request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Restore completed, restarting...\"}");
                shouldRestart = true;
            } else {
                Logger.error(MAINLOG, "Restore failed: configSaved=%d, entitiesSaved=%d", configSaved, entitiesSaved);
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to write config files\"}");
            }
        }
    });

    server.on("/api/config/factoryReset", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!isConfigFunctionsEnabled) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        bool configDeleted = storage.deleteFile(CONFIG_FILE_PATH);
        bool entitiesDeleted = storage.deleteFile(ENTITIES_FILE_PATH);

        bool success = configDeleted && entitiesDeleted;

        AsyncResponseStream *response = request->beginResponseStream("application/json");
        if (success) {
            Logger.info(MAINLOG, "Factory reset: config files deleted successfully, restarting...");
            response->print("{\"status\":\"success\",\"message\":\"Factory reset performed, restarting...\"}");
            shouldRestart = true;
        } else {
            Logger.error(MAINLOG, "Factory reset: some files could not be deleted (config=%d, entities=%d)",
                configDeleted, entitiesDeleted);
            response->print("{\"status\":\"error\",\"message\":\"Failed to delete config files, restart aborted\"}");
            shouldRestart = false;
        }
        request->send(response);
    });

    server.on("/api/wifiscan", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        JsonDocument doc(&psramAllocator);
        networkContext.scanNetworks(doc);
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    server.on("/api/testDiscord", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        if (request->hasParam("url", true)) {
            String testUrl = request->getParam("url", true)->value();

            bool success = notificationService.send(testUrl, "Your Discord integration is working perfectly!");

            if (success) {
                request->send(200, "text/plain", "OK");
            } else {
                request->send(500, "text/plain", "Send failed.");
            }
        } else {
            request->send(400, "text/plain", "Missing URL");
        }
    });

    server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        AsyncJsonResponse *response = new AsyncJsonResponse(false);
        JsonObject root = response->getRoot().to<JsonObject>();
        config.fillJson(root);
        response->setLength();
        request->send(response);
    });

    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, 
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {

        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        if (index == 0) request->_tempObject = new String("");
        String* body = (String*)request->_tempObject;
        for (size_t i = 0; i < len; i++) *body += (char)data[i];

        if (index + len == total) {
            Logger.debug(MAINLOG, "Received new config: %s", body);
            JsonDocument doc(&psramAllocator);
            deserializeJson(doc, *body);

            config_t newConfig = config.fromJsonObject(doc.as<JsonObject>());
            
            config_t currentConfig = config.getConfig();

            bool wifiChanged = (!newConfig.wifiSSID.equals(currentConfig.wifiSSID) || !newConfig.wifiPassword.equals(currentConfig.wifiPassword)) 
                && newConfig.wifiSSID.length() > 0 && newConfig.wifiPassword.length() > 0;
            if (wifiChanged) {
                if (!networkContext.connect(newConfig.wifiSSID, newConfig.wifiPassword, true)) {
                    AsyncJsonResponse *response = new AsyncJsonResponse(false);
                    response->setCode(400);
                    JsonObject doc = response->getRoot().to<JsonObject>();
                    doc["status"] = "error";
                    doc["message"] = "Failed to connect WiFi: " + newConfig.wifiSSID;
                    response->setLength();
                    request->send(response);
                    delete body; request->_tempObject = nullptr;
                    return;

                }
            }

            newConfig.hostname = Api::cleanHostname(newConfig.hostname);
            if (storage.writeFile(CONFIG_FILE_PATH, config.toJson(newConfig))) {
                shouldRestart = isAPMode || wifiChanged || !newConfig.hostname.equals(currentConfig.hostname);
                AsyncJsonResponse *response = new AsyncJsonResponse(false);
                JsonObject doc = response->getRoot().to<JsonObject>();
                doc["status"] = "success";
                doc["message"] = "Config saved successfully";
                doc["restarting"] = shouldRestart;
                response->setLength();
                request->send(response);
                Logger.info(MAINLOG, "Config saved successfully%s", shouldRestart ? ", restarting..." : "");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save config\"}");
            }

            delete body; request->_tempObject = nullptr;
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body provided\"}");
        }
    });
}

void Api::registerCommonEndpoints() {
}

void Api::registerServicesEndpoints() {
    server.on("/api/summary", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", monitoringService.summary());
    });

    /*server.on("/api/monitoredentities", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc(&psramAllocator);
        JsonArray array = doc.to<JsonArray>();
        monitoredEntityRepository.fillJsonArray(array);
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });*/
    server.on("/api/monitoredentities", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        
        response->print("[");
        
        auto entities = monitoredEntityRepository.getEntitiesSnapshot();
        for (size_t i = 0; i < entities.size(); i++) {
            JsonDocument itemDoc;
            
            itemDoc["name"] = entities[i].getName();
            itemDoc["address"] = entities[i].getAddress();
            itemDoc["icon"] = entities[i].getIcon();
            itemDoc["isOnline"] = entities[i].isOnline();
            itemDoc["notification"] = entities[i].getNotification();
            itemDoc["disabled"] = entities[i].isDisabled();
            itemDoc["lastChanged"] = entities[i].getLastChanged();

            serializeJson(itemDoc, *response);
            
            if (i < entities.size() - 1) {
                response->print(",");
            }
        }
        
        response->print("]");
        request->send(response);
    });

    server.on("^\\/api\\/monitoredentity\\/([a-zA-Z0-9 _\\-%]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String name = request->pathArg(0);
        MonitoredEntity entity;
        
        if (monitoredEntityRepository.getEntity(name, entity)) {
            AsyncJsonResponse *response = new AsyncJsonResponse(false);
            JsonObject root = response->getRoot().to<JsonObject>();
            entity.fillJson(root); 
            response->setLength();
            request->send(response);
        } else {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        }
    });

    server.on("^\\/api\\/monitoredentity\\/([a-zA-Z0-9 _\\-%]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        String name = request->pathArg(0);
        
        if (monitoredEntityRepository.removeEntity(name)) {
            request->send(200, "application/json", "{\"status\":\"deleted\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        }
    });

    server.on("/api/monitoredentity", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) request->_tempObject = new String("");
        String* buffer = (String*)request->_tempObject;
        for (size_t i = 0; i < len; i++) *buffer += (char)data[i];

        if (index + len == total) {
            JsonDocument doc(&psramAllocator);
            deserializeJson(doc, *buffer);
            
            MonitoredEntity newEntity;
            if (newEntity.fromJsonObject(doc.as<JsonObject>())) {
                if (!monitoredEntityRepository.hasEntity(newEntity.getName())) {
                    monitoredEntityRepository.addEntity(newEntity);
                    request->send(201, "application/json", "{\"status\":\"created\"}");
                } else {
                    request->send(409, "application/json", "{\"error\":\"Already exists\"}");
                }
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            }
            delete buffer;
            request->_tempObject = nullptr;
        }
    });

    server.on("^\\/api\\/monitoredentity\\/([a-zA-Z0-9 _\\-%]+)$", HTTP_PUT, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        
        if (index == 0) request->_tempObject = new String("");
        String* buffer = (String*)request->_tempObject;
        for (size_t i = 0; i < len; i++) *buffer += (char)data[i];

        if (index + len == total) {
            String oldName = request->pathArg(0);
            oldName.replace("%20", " "); 
            oldName.replace("+", " ");

            JsonDocument doc(&psramAllocator);
            deserializeJson(doc, *buffer);
            delete buffer;
            request->_tempObject = nullptr;
            
            MonitoredEntity updatedData;
            if (updatedData.fromJsonObject(doc.as<JsonObject>())) {
                MonitoredEntity existingEntity;
                if (!monitoredEntityRepository.getEntity(oldName, existingEntity)) {
                    request->send(404, "application/json", "{\"error\":\"Entity not found\"}");
                    return;
                }
                updatedData.setLastChanged(existingEntity.getLastChanged());
                updatedData.setOnlineStatus(existingEntity.isOnline());
                updatedData.resetFailCount();
                if (monitoredEntityRepository.updateEntity(oldName, updatedData)) {
                    request->send(200, "application/json", "{\"status\":\"updated\"}");
                } else {
                    request->send(404, "application/json", "{\"error\":\"Error when saving entity\"}");
                }
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            }
        }
    });
}

void Api::registerSystemInformationEndpoints() {
    server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        Logger.debug(MAINLOG, "Health check request");
        request->send(200, "application/json", "{\"status\":\"UP\"}");
    });

    server.on("/api/sysinfo", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncJsonResponse *response = new AsyncJsonResponse(false);
        JsonObject doc = response->getRoot().to<JsonObject>();
        float tsens_out = 0.0;
        esp_err_t error = temperature_sensor_get_celsius(temp_sensor, &tsens_out);
        if (error == ESP_OK) {
            doc["temperature"] = tsens_out;
        } else {
            Logger.error(MAINLOG, "Failed to read temperature sensor: %s", esp_err_to_name(error));
        }
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["rssi"] = WiFi.RSSI();
        doc["uptime"] = millis() / 1000;
        doc["version"] = FIRMWARE_VERSION;
        doc["buildDate"] = BUILD_TIMESTAMP;
        doc["commit"] = COMMIT_HASH;
        doc["configEnabled"] = isConfigFunctionsEnabled;
        response->setLength();
        request->send(response);
    });
}

String Api::cleanHostname(const String &rawInput) {
    String input = rawInput;

    // 1. Replace multi-byte UTF-8 Hungarian accents (lowercase & uppercase)
    input.replace("á", "a"); input.replace("Á", "a");
    input.replace("é", "e"); input.replace("É", "e");
    input.replace("í", "i"); input.replace("Í", "i");
    input.replace("ó", "o"); input.replace("Ó", "o");
    input.replace("ö", "o"); input.replace("Ö", "o");
    input.replace("ő", "o"); input.replace("Ő", "o");
    input.replace("ú", "u"); input.replace("Ú", "u");
    input.replace("ü", "u"); input.replace("Ü", "u");
    input.replace("ű", "u"); input.replace("Ű", "u");

    String output = "";
    output.reserve(input.length() + 1);

    for (size_t i = 0; i < input.length(); i++) {
        uint8_t c = (uint8_t)input[i];

        // ISO-8859-2 / Windows-1250 single-byte fallback mappings
        if (c == 0xE1 || c == 0xC1) { c = 'a'; }
        else if (c == 0xE9 || c == 0xC9) { c = 'e'; }
        else if (c == 0xED || c == 0xCD) { c = 'i'; }
        else if (c == 0xF3 || c == 0xD3 || c == 0xF6 || c == 0xD6 || c == 0xF5 || c == 0xD5) { c = 'o'; }
        else if (c == 0xFA || c == 0xDA || c == 0xFC || c == 0xDC || c == 0xFB || c == 0xDB) { c = 'u'; }

        // Lowercase ASCII
        if (c >= 'A' && c <= 'Z') {
            c = c + ('a' - 'A');
        }

        // Allowed chars: a-z, 0-9, '-', '_'
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            output += (char)c;
        } else if (c == ' ' || c == '.' || c == '/') {
            // Avoid consecutive underscores
            if (output.length() > 0 && output[output.length() - 1] != '_' && output[output.length() - 1] != '-') {
                output += '_';
            }
        }
    }

    // Trim leading '_' and '-'
    while (output.length() > 0 && (output.startsWith("_") || output.startsWith("-"))) {
        output.remove(0, 1);
    }

    // Trim trailing '_' and '-'
    while (output.length() > 0 && (output.endsWith("_") || output.endsWith("-"))) {
        output.remove(output.length() - 1);
    }

    // Enforce max DNS label length (63 characters)
    if (output.length() > 63) {
        output = output.substring(0, 63);
        while (output.length() > 0 && (output.endsWith("_") || output.endsWith("-"))) {
            output.remove(output.length() - 1);
        }
    }

    if (output.length() == 0) {
        output = DEFAULT_HOSTNAME;
    }

    return output;
}
