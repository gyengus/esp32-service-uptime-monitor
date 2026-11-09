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
#include "WsService.h"
#include "LedService.h"
#include "PSRAMAllocator.h"
#include "WebArchiveExtractor.h"

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

        if (configJson.length() == 0 || entitiesJson.length() == 0) {
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
            Logger.error(MAINLOG, "Storage not available");
            request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (index == 0) request->_tempObject = new String("");

        String* body = (String*)request->_tempObject;
        if (body) {
            for (size_t i = 0; i < len; i++) *body += (char)data[i];
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

    server.on("/api/config/test-led", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!isConfigFunctionsEnabled && !isAPMode) {
                request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
                return;
            }

            if (index == 0) request->_tempObject = new String("");
            String* body = (String*)request->_tempObject;
            for (size_t i = 0; i < len; i++) *body += (char)data[i];

            if (index + len == total) {
                uint8_t brightness = config.getConfig().ledBrightness;
                uint32_t duration = 3000;

                if (body && body->length() > 0) {
                    JsonDocument doc(&psramAllocator);
                    DeserializationError err = deserializeJson(doc, *body);
                    if (!err) {
                        if (doc["brightness"].is<int>() || doc["brightness"].is<uint8_t>()) {
                            int b = doc["brightness"].as<int>();
                            if (b < 0) b = 0;
                            if (b > 100) b = 100;
                            brightness = (uint8_t)b;
                        }
                        if (doc["duration"].is<int>() || doc["duration"].is<uint32_t>()) {
                            int d = doc["duration"].as<int>();
                            if (d > 0 && d <= 10000) duration = (uint32_t)d;
                        }
                    }
                }

                ledService.startTest(brightness, duration);

                AsyncJsonResponse *response = new AsyncJsonResponse(false);
                JsonObject doc = response->getRoot().to<JsonObject>();
                doc["status"] = "success";
                doc["message"] = "LED test started";
                doc["brightness"] = brightness;
                doc["duration"] = duration;
                response->setLength();
                request->send(response);

                if (body) {
                    delete body;
                    request->_tempObject = nullptr;
                }
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
                if (!networkContext.connect(newConfig.wifiSSID, newConfig.wifiPassword, newConfig.hostname, WIFI_CONNECT_TIMEOUT, true)) {
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
            if (config.saveConfig(newConfig)) {
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

    server.on("/api/web/upload-archive", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isConfigFunctionsEnabled && !isAPMode && !request->hasHeader("X-PlatformIO-Upload")) {
            request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            return;
        }

        if (!storage.isAvailable()) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Storage not available\"}");
            return;
        }

        Logger.info(MAINLOG, "Web archive upload completed. Starting extraction...");

        wsService.closeAll();

        if (MonitoringTaskHandle != NULL) {
            vTaskSuspend(MonitoringTaskHandle);
        }
        xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY);

        bool success = WebArchiveExtractor::extractZipArchive("/web_update_tmp.zip", WEB_UI_PATH);

        storage.deleteFile("/web_update_tmp.zip");

        xSemaphoreGive(monitoredEntityRepository.getMutex());
        if (MonitoringTaskHandle != NULL) {
            vTaskResume(MonitoringTaskHandle);
        }

        if (success) {
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Web UI updated successfully\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to extract web archive\"}");
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isConfigFunctionsEnabled && !isAPMode && !request->hasHeader("X-PlatformIO-Upload")) {
            return;
        }
        if (!storage.isAvailable()) {
            return;
        }

        if (index == 0) {
            Logger.info(MAINLOG, "Receiving web archive: %s", filename.c_str());
            request->_tempFile = storage.getFile("/web_update_tmp.zip", FILE_WRITE);
        }

        if (request->_tempFile) {
            if (len) {
                request->_tempFile.write(data, len);
            }
            if (final) {
                request->_tempFile.close();
                Logger.info(MAINLOG, "Web archive received successfully.");
            }
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
            wsService.broadcastEntityDeleted(name);
            wsService.broadcastSummary();
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
                    wsService.broadcastEntityUpdate(newEntity);
                    wsService.broadcastSummary();
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
                    if (!oldName.equals(updatedData.getName())) {
                        wsService.broadcastEntityDeleted(oldName);
                    }
                    wsService.broadcastEntityUpdate(updatedData);
                    wsService.broadcastSummary();
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
