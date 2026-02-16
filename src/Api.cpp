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

    server.on("/api/testSlack", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        if (request->hasParam("url", true)) {
            String testUrl = request->getParam("url", true)->value();

            bool success = notificationService.send(testUrl, "🚀 *ESP32 Service Uptime Monitor*\nYour Slack integration is working perfectly!");
            
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

            newConfig.hostname = this->cleanHostname(newConfig.hostname);
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
        
        auto entities = monitoredEntityRepository.getEntities();
        for (size_t i = 0; i < entities.size(); i++) {
            JsonDocument itemDoc;
            
            itemDoc["name"] = entities[i].getName();
            itemDoc["address"] = entities[i].getAddress();
            itemDoc["icon"] = entities[i].getIcon();
            itemDoc["type"] = (entities[i].getType() == HEAD ? "HEAD" : "PING");
            itemDoc["isOnline"] = entities[i].isOnline();
            itemDoc["notification"] = entities[i].getNotification();
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
        MonitoredEntity* entity = monitoredEntityRepository.getEntity(name);
        
        if (entity != nullptr) {
            AsyncJsonResponse *response = new AsyncJsonResponse(false);
            JsonObject root = response->getRoot().to<JsonObject>();
            entity->fillJson(root); 
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
                if (monitoredEntityRepository.getEntity(newEntity.getName()) == nullptr) {
                    monitoredEntityRepository.addEntity(newEntity);
                    monitoredEntityRepository.save();
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

            JsonDocument doc(&psramAllocator);
            deserializeJson(doc, *buffer);
            delete buffer;
            request->_tempObject = nullptr;
            
            MonitoredEntity updatedData;
            if (updatedData.fromJsonObject(doc.as<JsonObject>())) {
                MonitoredEntity* existingEntity = monitoredEntityRepository.getEntity(oldName);
                if (existingEntity == nullptr) {
                    request->send(404, "application/json", "{\"error\":\"Entity not found\"}");
                    return;
                }
                updatedData.setLastChanged(existingEntity->getLastChanged());
                updatedData.setOnlineStatus(existingEntity->isOnline());
                updatedData.resetFailCount();
                if (monitoredEntityRepository.updateEntity(oldName, updatedData)) {
                    monitoredEntityRepository.save();
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

String Api::cleanHostname(String &input) {
    String output = "";
    input.toLowerCase();

    input.replace("á", "a"); input.replace("é", "e"); input.replace("í", "i");
    input.replace("ó", "o"); input.replace("ö", "o"); input.replace("ő", "o");
    input.replace("ú", "u"); input.replace("ü", "u"); input.replace("ű", "u");

    for (uint8_t i = 0; i < input.length(); i++) {
        char c = input[i];
        // 'a'
        if (c == 0xA1 || c == 0xE1) { output += 'a'; continue; } 
        // 'e'
        if (c == 0xE9) { output += 'e'; continue; }
        // 'i'
        if (c == 0xED) { output += 'i'; continue; }
        // 'o'
        if (c == 0xF3 || c == 0xF2 || c == 0xF4 || c == 0xF6 || c == 0xF5) { output += 'o'; continue; }
        // 'u'
        if (c == 0xFA || c == 0xF9 || c == 0xFB || c == 0xFC || c == 0xFB) { output += 'u'; continue; }

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            output += c;
        } else if (c == ' ') {
            if (output.length() > 0 && output[output.length() - 1] != '_') {
                output += '_';
            }
        }
    }
    
    while (output.startsWith("_") || output.startsWith("-")) output.remove(0, 1);
    while (output.endsWith("_") || output.endsWith("-")) output.remove(output.length() - 1);

    if (output.length() == 0) output = DEFAULT_HOSTNAME;
    return output;
}
