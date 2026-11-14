#include "Api.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Elog.h>
#include <Update.h>
#include <WiFi.h>
#include <cctype>
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
#include "SystemInfo.h"
#include "WebArchiveExtractor.h"
#include "WebSecurity.h"

static PSRAMAllocator psramAllocator;

static const size_t MAX_CONFIG_BODY_SIZE = 4096;
static const size_t MAX_ENTITY_BODY_SIZE = 4096;
static const size_t MAX_RESTORE_BODY_SIZE = 32768;

static uint8_t failedOtaAttempts = 0;
static uint32_t otaLockoutUntil = 0;

static bool constantTimeCompare(const String &a, const String &b) {
    if (a.length() != b.length()) return false;
    volatile uint8_t result = 0;
    for (size_t i = 0; i < a.length(); i++) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}

static bool isConfigAuthorized(AsyncWebServerRequest *request = nullptr) {
    if (!isConfigFunctionsEnabled && !isAPMode) {
        return false;
    }
    if (request != nullptr && !WebSecurity::isRequestOriginTrusted(request)) {
        return false;
    }
    if (isConfigFunctionsEnabled) {
        resetConfigModeActivity();
    }
    return true;
}

static bool isUploadAuthorized(AsyncWebServerRequest *request) {
    if (isAPMode) {
        return false;
    }
    if (request && request->hasHeader("X-PlatformIO-Upload")) {
        uint32_t now = millis();
        if (failedOtaAttempts >= 5) {
            if ((int32_t)(now - otaLockoutUntil) < 0) {
                Logger.warning(MAINLOG, "OTA token verification locked out due to repeated failures");
                return false;
            }
            failedOtaAttempts = 0;
        }

        String otaToken = config.getOtaToken();
        if (otaToken.length() >= 8) {
            const AsyncWebHeader *h = request->getHeader("X-PlatformIO-Upload");
            if (h != nullptr && constantTimeCompare(h->value(), otaToken)) {
                failedOtaAttempts = 0;
                return true;
            }
        }
        failedOtaAttempts++;
        if (failedOtaAttempts >= 5) {
            otaLockoutUntil = millis() + 60000;
            Logger.warning(MAINLOG, "OTA token verification failed (attempt %u/5), locking out for 60s", failedOtaAttempts);
        } else {
            Logger.warning(MAINLOG, "OTA token verification failed (attempt %u/5)", failedOtaAttempts);
        }
        return false;
    }
    return isConfigAuthorized(request);
}

static String urlDecode(const String &input) {
    String decoded = "";
    decoded.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        if (input[i] == '%' && i + 2 < input.length()) {
            char a = input[++i];
            char b = input[++i];
            decoded += (char)((std::isdigit(static_cast<unsigned char>(a)) ? a - '0' : std::toupper(static_cast<unsigned char>(a)) - 'A' + 10) * 16 +
                             (std::isdigit(static_cast<unsigned char>(b)) ? b - '0' : std::toupper(static_cast<unsigned char>(b)) - 'A' + 10));
        } else if (input[i] == '+') {
            decoded += ' ';
        } else {
            decoded += input[i];
        }
    }
    return decoded;
}

static bool isValidMd5Hex(const String &hash) {
    if (hash.length() != 32) {
        return false;
    }
    for (size_t i = 0; i < hash.length(); i++) {
        if (!std::isxdigit(static_cast<unsigned char>(hash.charAt(i)))) {
            return false;
        }
    }
    return true;
}

Api::Api(AsyncWebServer &server) : server(server) {}

void Api::registerEndpoints() {
    registerCommonEndpoints();
    registerConfigEndpoints();
    registerOtaEndpoints();
    if (!isAPMode) registerServicesEndpoints();
    registerSystemInformationEndpoints();
}

void Api::registerConfigEndpoints() {
    /**
     * @openapi
     * /api/config/backup:
     *   get:
     *     tags: [Configuration]
     *     summary: Export configuration and monitored entities backup
     *     responses:
     *       '200':
     *         description: Backup JSON payload
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/BackupPayload'
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: Failed to read or parse config files
     */
    server.on("/api/config/backup", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!isConfigAuthorized(request)) {
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
        response->addHeader("Cache-Control", "no-store");
        response->print(backupJson);
        request->send(response);
    });

    /**
     * @openapi
     * /api/config/restore:
     *   post:
     *     tags: [Configuration]
     *     summary: Restore configuration and monitored entities from backup file
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
     *                 description: Backup JSON file containing config and entities
     *     responses:
     *       '200':
     *         description: Restore completed and device restarting
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/StatusResponse'
     *       '400':
     *         description: Invalid JSON or missing config/entities keys
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: Storage unavailable or failed to write config files
     */
    server.on("/api/config/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isConfigAuthorized(request)) {
            if (index == 0) request->send(403, "text/plain", "Forbidden");
            return;
        }

        if (!storage.isAvailable()) {
            Logger.error(MAINLOG, "Storage not available");
            if (index == 0) request->send(500, "text/plain", "Storage not available");
            return;
        }

        if (index == 0) {
            request->_tempObject = new String("");
            request->onDisconnect([request]() {
                if (request->_tempObject) {
                    delete (String*)request->_tempObject;
                    request->_tempObject = nullptr;
                }
            });
        }

        String* body = (String*)request->_tempObject;
        if (body) {
            if (body->length() + len > MAX_RESTORE_BODY_SIZE) {
                request->send(413, "application/json", "{\"status\":\"error\",\"message\":\"Payload Too Large\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }
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

            if (!root["entities"].is<JsonArray>() || !root["config"].is<JsonObject>()) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid structure in backup\"}");
                delete body; request->_tempObject = nullptr;
                return;
            }

            config_t restoredCfg = config.fromJsonObject(root["config"].as<JsonObject>());
            restoredCfg.hostname = Api::cleanHostname(restoredCfg.hostname);
            if (restoredCfg.sntpServer.length() == 0 || restoredCfg.sntpServer.length() > 64) {
                restoredCfg.sntpServer = DEFAULT_SNTP_SERVER;
            }
            if (restoredCfg.checkInterval < 5 || restoredCfg.checkInterval > 86400) {
                restoredCfg.checkInterval = DEFAULT_CHECK_INTERVAL_SECONDS;
            }
            if (restoredCfg.offsetHours < -12 || restoredCfg.offsetHours > 14) {
                restoredCfg.offsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
            }
            if (restoredCfg.logLevel > 11) {
                restoredCfg.logLevel = DEFAULT_LOG_LEVEL;
            }

            JsonDocument validEntitiesDoc(&psramAllocator);
            JsonArray validEntities = validEntitiesDoc.to<JsonArray>();
            JsonArray entitiesArr = root["entities"].as<JsonArray>();

            for (JsonObject eObj : entitiesArr) {
                MonitoredEntity entity;
                if (entity.fromJsonObject(eObj) && entity.isValidForUserInput()) {
                    JsonObject outObj = validEntities.add<JsonObject>();
                    entity.fillJson(outObj);
                } else {
                    const char* nameStr = eObj["name"] | "unnamed";
                    Logger.warning(MAINLOG, "Restore: skipped invalid entity '%s'", nameStr);
                }
            }

            JsonDocument cleanConfigDoc(&psramAllocator);
            JsonObject cleanCfgObj = cleanConfigDoc.to<JsonObject>();
            cleanCfgObj["wifiSSID"] = restoredCfg.wifiSSID;
            cleanCfgObj["wifiPassword"] = restoredCfg.wifiPassword;
            cleanCfgObj["sntpServer"] = restoredCfg.sntpServer;
            cleanCfgObj["offsetHours"] = restoredCfg.offsetHours;
            cleanCfgObj["checkInterval"] = restoredCfg.checkInterval;
            cleanCfgObj["discordWebhookURL"] = restoredCfg.discordWebhookURL;
            cleanCfgObj["hostname"] = restoredCfg.hostname;
            cleanCfgObj["ledBrightness"] = restoredCfg.ledBrightness;
            cleanCfgObj["otaToken"] = restoredCfg.otaToken;
            cleanCfgObj["logLevel"] = restoredCfg.logLevel;
            cleanCfgObj["logLevelName"] = Config::logLevelToString(restoredCfg.logLevel);

            String configJson;
            String entitiesJson;
            serializeJson(cleanCfgObj, configJson);
            serializeJson(validEntities, entitiesJson);

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

    /**
     * @openapi
     * /api/config/factoryReset:
     *   post:
     *     tags: [Configuration]
     *     summary: Delete configuration and entity files and reboot into AP mode
     *     responses:
     *       '200':
     *         description: Factory reset result
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/StatusResponse'
     *       '403':
     *         description: Configuration mode disabled
     */
    server.on("/api/config/factoryReset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!isConfigAuthorized(request)) {
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

    /**
     * @openapi
     * /api/wifiscan:
     *   get:
     *     tags: [Configuration]
     *     summary: Trigger or poll non-blocking Wi-Fi network scan
     *     responses:
     *       '200':
     *         description: Current scan state and discovered networks
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/WifiScanResponse'
     *       '403':
     *         description: Configuration mode disabled
     */
    server.on("/api/wifiscan", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isConfigAuthorized(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        JsonDocument doc(&psramAllocator);
        networkContext.scanNetworks(doc);
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    /**
     * @openapi
     * /api/testDiscord:
     *   post:
     *     tags: [Configuration]
     *     summary: Send a test message to a Discord webhook URL
     *     requestBody:
     *       required: true
     *       content:
     *         application/x-www-form-urlencoded:
     *           schema:
     *             type: object
     *             required: [url]
     *             properties:
     *               url:
     *                 type: string
     *                 example: https://discord.com/api/webhooks/...
     *     responses:
     *       '200':
     *         description: Notification sent successfully (OK)
     *       '400':
     *         description: Missing url parameter
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: Webhook request failed
     */
    server.on("/api/testDiscord", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!isConfigAuthorized(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }

        if (request->hasParam("url", true)) {
            String testUrl = request->getParam("url", true)->value();
            testUrl.trim();
            if (testUrl.length() == 0 || testUrl.indexOf("******") != -1) {
                testUrl = config.getConfig().discordWebhookURL;
            }

            if (!testUrl.startsWith("https://discord.com/api/webhooks/") &&
                !testUrl.startsWith("https://discordapp.com/api/webhooks/")) {
                request->send(400, "text/plain", "Invalid Discord webhook URL");
                return;
            }

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

    /**
     * @openapi
     * /api/config/test-led:
     *   post:
     *     tags: [Configuration]
     *     summary: Trigger a temporary cyan pulse test on the onboard RGB LED
     *     requestBody:
     *       required: false
     *       content:
     *         application/json:
     *           schema:
     *             type: object
     *             properties:
     *               brightness:
     *                 type: integer
     *                 minimum: 0
     *                 maximum: 100
     *                 example: 50
     *               duration:
     *                 type: integer
     *                 minimum: 1
     *                 maximum: 10000
     *                 description: Test duration in milliseconds
     *                 example: 3000
     *     responses:
     *       '200':
     *         description: LED test started
     *       '400':
     *         description: RGB LED not supported on this board
     *       '403':
     *         description: Configuration mode disabled
     */
    server.on("/api/config/test-led", HTTP_POST, [this](AsyncWebServerRequest *request) {
        String* body = (String*)request->_tempObject;

        if (!isConfigAuthorized(request)) {
            if (body) {
                delete body;
                request->_tempObject = nullptr;
            }
            request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            return;
        }

        if (body && body->length() > 0 && !WebSecurity::isJsonContentType(request)) {
            delete body;
            request->_tempObject = nullptr;
            request->send(415, "application/json", "{\"status\":\"error\",\"message\":\"Unsupported Media Type: application/json required\"}");
            return;
        }

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

        if (body) {
            delete body;
            request->_tempObject = nullptr;
        }

#ifdef HAS_RGB_LED
        ledService.startTest(brightness, duration);

        AsyncJsonResponse *response = new AsyncJsonResponse(false);
        JsonObject doc = response->getRoot().to<JsonObject>();
        doc["status"] = "success";
        doc["message"] = "LED test started";
        doc["brightness"] = brightness;
        doc["duration"] = duration;
        response->setLength();
        request->send(response);
#else
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"RGB LED not supported on this board\"}");
#endif
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (total > 1024) return;
        if (index == 0) {
            request->_tempObject = new String("");
            request->onDisconnect([request]() {
                if (request->_tempObject) {
                    delete (String*)request->_tempObject;
                    request->_tempObject = nullptr;
                }
            });
        }
        String* body = (String*)request->_tempObject;
        if (body) {
            for (size_t i = 0; i < len; i++) *body += (char)data[i];
        }
    });

    /**
     * @openapi
     * /api/config:
     *   get:
     *     tags: [Configuration]
     *     summary: Get current device configuration
     *     responses:
     *       '200':
     *         description: Current configuration
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/Config'
     *       '403':
     *         description: Configuration mode disabled
     */
    server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!isConfigAuthorized(request)) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        AsyncJsonResponse *response = new AsyncJsonResponse(false);
        JsonObject root = response->getRoot().to<JsonObject>();
        config.fillJson(root);
        root["configEnabled"] = isConfigFunctionsEnabled;
        root["configTimeoutRemaining"] = getConfigModeRemainingSeconds();

        if (root["wifiPassword"].as<String>().length() > 0) {
            root["wifiPassword"] = "******";
        }
        if (root["otaToken"].as<String>().length() > 0) {
            root["otaToken"] = "******";
        }
        String webhook = root["discordWebhookURL"].as<String>();
        if (webhook.length() > 0) {
            int lastSlash = webhook.lastIndexOf('/');
            if (lastSlash > 0 && lastSlash + 1 < (int)webhook.length()) {
                root["discordWebhookURL"] = webhook.substring(0, lastSlash + 1) + "******";
            }
        }

        response->setLength();
        request->send(response);
    });

    /**
     * @openapi
     * /api/config:
     *   post:
     *     tags: [Configuration]
     *     summary: Save updated device configuration
     *     requestBody:
     *       required: true
     *       content:
     *         application/json:
     *           schema:
     *             $ref: '#/components/schemas/Config'
     *     responses:
     *       '200':
     *         description: Configuration saved
     *         content:
     *           application/json:
     *             schema:
     *               type: object
     *               properties:
     *                 status:
     *                   type: string
     *                   example: success
     *                 message:
     *                   type: string
     *                 restarting:
     *                   type: boolean
     *       '400':
     *         description: Wi-Fi connection failed or missing body
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: Failed to save configuration
     */
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL, 
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {

        if (!isConfigAuthorized(request)) {
            if (index == 0) {
                request->send(403, "text/plain", "Forbidden");
            }
            return;
        }

        if (!WebSecurity::isJsonContentType(request)) {
            if (index == 0) {
                request->send(415, "application/json", "{\"status\":\"error\",\"message\":\"Unsupported Media Type: application/json required\"}");
            }
            return;
        }

        if (total > MAX_CONFIG_BODY_SIZE) {
            if (index == 0) {
                request->send(413, "application/json", "{\"status\":\"error\",\"message\":\"Payload Too Large\"}");
            }
            return;
        }

        if (index == 0) {
            request->_tempObject = new String("");
            request->onDisconnect([request]() {
                if (request->_tempObject) {
                    delete (String*)request->_tempObject;
                    request->_tempObject = nullptr;
                }
            });
        }
        String* body = (String*)request->_tempObject;
        if (body) {
            for (size_t i = 0; i < len; i++) *body += (char)data[i];
        }

        if (index + len == total) {
            if (!body || body->length() == 0) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No body provided\"}");
                if (body) {
                    delete body;
                    request->_tempObject = nullptr;
                }
                return;
            }

            Logger.debug(MAINLOG, "Received new config (%u bytes)", (unsigned int)body->length());
            JsonDocument doc(&psramAllocator);
            DeserializationError error = deserializeJson(doc, *body);
            if (error) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
                delete body;
                request->_tempObject = nullptr;
                return;
            }

            config_t newConfig = config.fromJsonObject(doc.as<JsonObject>());
            
            config_t currentConfig = config.getConfig();

            if (newConfig.wifiPassword.length() == 0 || newConfig.wifiPassword.equals("******")) {
                newConfig.wifiPassword = currentConfig.wifiPassword;
            }
            if (newConfig.otaToken.length() == 0 || newConfig.otaToken.equals("******")) {
                newConfig.otaToken = currentConfig.otaToken;
            }
            if (newConfig.discordWebhookURL.indexOf("******") != -1) {
                newConfig.discordWebhookURL = currentConfig.discordWebhookURL;
            }

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
        }
    });

    /**
     * @openapi
     * /api/web/upload-archive:
     *   post:
     *     tags: [Updates]
     *     summary: Upload and extract a web.zip UI archive into storage
     *     parameters:
     *       - in: header
     *         name: X-PlatformIO-Upload
     *         required: false
     *         schema:
     *           type: string
     *         description: Optional header used by PlatformIO uploadweb script to bypass lock
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
     *                 description: Minified web.zip archive
     *     responses:
     *       '200':
     *         description: Web UI updated successfully
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/StatusResponse'
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: Storage unavailable or extraction failed
     */
    server.on("/api/web/upload-archive", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!isUploadAuthorized(request)) {
            request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            return;
        }

        if (!storage.isAvailable()) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Storage not available\"}");
            return;
        }

        Logger.info(MAINLOG, "Web archive upload completed. Starting extraction...");

        wsService.closeAll();

        xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY);
        if (MonitoringTaskHandle != NULL) {
            vTaskSuspend(MonitoringTaskHandle);
        }

        bool success = WebArchiveExtractor::extractZipArchive("/web_update_tmp.zip", WEB_UI_PATH);
        if (success) {
            storage.writeFile(String(WEB_UI_PATH) + "/.version", String(FIRMWARE_VERSION) + "-" + String(COMMIT_HASH));
            isConfigFunctionsEnabled = true;
            resetConfigModeActivity();
        }

        storage.deleteFile("/web_update_tmp.zip");

        if (MonitoringTaskHandle != NULL) {
            vTaskResume(MonitoringTaskHandle);
        }
        xSemaphoreGive(monitoredEntityRepository.getMutex());

        if (success) {
            wsService.broadcastSysinfo();
            request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Web UI updated successfully\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to extract web archive\"}");
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isUploadAuthorized(request)) {
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

void Api::restoreOtaState() {
    if (_otaStorageUnmounted) {
        storage.mount();
        _otaStorageUnmounted = false;
    }
    if (_otaTaskSuspended && MonitoringTaskHandle != NULL) {
        vTaskResume(MonitoringTaskHandle);
        _otaTaskSuspended = false;
    }
    if (_otaMutexHeld) {
        xSemaphoreGive(monitoredEntityRepository.getMutex());
        _otaMutexHeld = false;
    }
    isOtaUpdating = false;
}

void Api::registerOtaEndpoints() {
    /**
     * @openapi
     * /ota/start:
     *   get:
     *     tags: [Updates]
     *     summary: Initialize a firmware OTA upload session
     *     parameters:
     *       - in: query
     *         name: mode
     *         required: false
     *         schema:
     *           type: string
     *           enum: [fr]
     *           default: fr
     *         description: OTA target mode ('fr' for firmware flash)
     *       - in: query
     *         name: hash
     *         required: false
     *         schema:
     *           type: string
     *         description: Optional 32-character hexadecimal MD5 checksum of the firmware binary
     *       - in: header
     *         name: X-PlatformIO-Upload
     *         required: false
     *         schema:
     *           type: string
     *         description: Optional header used by PlatformIO OTA upload script to bypass lock
     *     responses:
     *       '200':
     *         description: OTA session ready
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/StatusResponse'
     *       '400':
     *         description: Invalid OTA mode or MD5 checksum format
     *       '403':
     *         description: Configuration mode disabled
     *       '409':
     *         description: Another OTA update is already in progress
     */
    server.on("/ota/start", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!isUploadAuthorized(request)) {
            request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            return;
        }

        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            if (!mode.equals("fr")) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unsupported OTA mode; only 'fr' is supported\"}");
                return;
            }
        }

        String hash = "";
        if (request->hasParam("hash")) {
            hash = request->getParam("hash")->value();
            hash.trim();
            if (hash.length() > 0) {
                if (!isValidMd5Hex(hash)) {
                    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid MD5 hash format\"}");
                    return;
                }
                hash.toLowerCase();
            }
        }

        if (Update.isRunning() || isOtaUpdating) {
            request->send(409, "application/json", "{\"status\":\"error\",\"message\":\"OTA update already in progress\"}");
            return;
        }

        _pendingOtaMd5 = hash;
        _otaUploadError = "";
        _otaReceivedBytes = 0;

        request->send(200, "application/json", "{\"status\":\"ready\",\"message\":\"OTA session initialized\"}");
    });

    /**
     * @openapi
     * /ota/upload:
     *   post:
     *     tags: [Updates]
     *     summary: Upload and flash a firmware binary via native ESP32 OTA
     *     parameters:
     *       - in: header
     *         name: X-PlatformIO-Upload
     *         required: false
     *         schema:
     *           type: string
     *         description: Optional header used by PlatformIO OTA upload script to bypass lock
     *     requestBody:
     *       required: true
     *       content:
     *         multipart/form-data:
     *           schema:
     *             type: object
     *             required: [file]
     *             properties:
     *               MD5:
     *                 type: string
     *                 description: Optional 32-character hexadecimal MD5 checksum
     *               file:
     *                 type: string
     *                 format: binary
     *                 description: Compiled firmware .bin image
     *     responses:
     *       '200':
     *         description: Firmware flashed successfully, device rebooting
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/StatusResponse'
     *       '400':
     *         description: Empty firmware image or invalid request
     *       '403':
     *         description: Configuration mode disabled
     *       '500':
     *         description: OTA flash initialization, write, or verification failed
     */
    server.on("/ota/upload", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!isUploadAuthorized(request)) {
            request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            return;
        }

        if (_otaUploadError.length() > 0) {
            restoreOtaState();
            _pendingOtaMd5 = "";
            Logger.error(MAINLOG, "OTA firmware update failed: %s", _otaUploadError.c_str());

            JsonDocument doc;
            doc["status"] = "error";
            doc["message"] = _otaUploadError;
            String response;
            serializeJson(doc, response);
            request->send(500, "application/json", response);
            return;
        }

        if (_otaReceivedBytes == 0 || Update.hasError()) {
            String err = Update.hasError() ? String(Update.errorString()) : String("No firmware binary received");
            if (Update.isRunning()) {
                Update.abort();
            }
            restoreOtaState();
            _pendingOtaMd5 = "";

            JsonDocument doc;
            doc["status"] = "error";
            doc["message"] = err;
            String response;
            serializeJson(doc, response);
            request->send(_otaReceivedBytes == 0 ? 400 : 500, "application/json", response);
            return;
        }

        Logger.info(MAINLOG, "OTA firmware update succeeded (%u bytes), scheduling reboot...", (unsigned int)_otaReceivedBytes);
        shouldRestart = true;
        request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Firmware updated successfully. Rebooting...\"}");
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!isUploadAuthorized(request)) {
            return;
        }

        if (index == 0) {
            _otaUploadError = "";
            _otaReceivedBytes = 0;

            if (Update.isRunning() || isOtaUpdating) {
                _otaUploadError = "OTA update already in progress";
                return;
            }

            if (_pendingOtaMd5.length() == 0 && request->hasParam("MD5", true)) {
                String md5Param = request->getParam("MD5", true)->value();
                md5Param.trim();
                if (md5Param.length() > 0) {
                    if (!isValidMd5Hex(md5Param)) {
                        _otaUploadError = "Invalid MD5 hash format";
                        return;
                    }
                    md5Param.toLowerCase();
                    _pendingOtaMd5 = md5Param;
                }
            }

            isOtaUpdating = true;
            wsService.closeAll();

            xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY);
            _otaMutexHeld = true;

            if (MonitoringTaskHandle != NULL) {
                vTaskSuspend(MonitoringTaskHandle);
                _otaTaskSuspended = true;
            }

            if (storage.unmount()) {
                _otaStorageUnmounted = true;
            }

            request->onDisconnect([this]() {
                if (isOtaUpdating && !shouldRestart) {
                    Logger.warning(MAINLOG, "OTA client disconnected mid-upload, aborting update...");
                    if (Update.isRunning()) {
                        Update.abort();
                    }
                    restoreOtaState();
                    _pendingOtaMd5 = "";
                }
            });

            Logger.info(MAINLOG, "Starting OTA firmware update: %s", filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                _otaUploadError = String("OTA begin failed: ") + Update.errorString();
                restoreOtaState();
                return;
            }

            if (_pendingOtaMd5.length() > 0) {
                Update.setMD5(_pendingOtaMd5.c_str());
            }
        }

        if (_otaUploadError.length() > 0) {
            return;
        }

        if (len > 0) {
            size_t written = Update.write(data, len);
            if (written != len) {
                _otaUploadError = String("OTA write failed: ") + Update.errorString();
                Update.abort();
                restoreOtaState();
                return;
            }
            _otaReceivedBytes += written;
        }

        if (final) {
            if (_otaReceivedBytes == 0) {
                _otaUploadError = "Empty firmware binary";
                Update.abort();
                restoreOtaState();
                return;
            }
            if (!Update.end(true)) {
                _otaUploadError = String("OTA finalize failed: ") + Update.errorString();
                restoreOtaState();
                return;
            }
            _pendingOtaMd5 = "";
        }
    });
}

void Api::registerCommonEndpoints() {
}

void Api::registerServicesEndpoints() {
    /**
     * @openapi
     * /api/summary:
     *   get:
     *     tags: [Monitoring]
     *     summary: Get monitored entities count summary
     *     responses:
     *       '200':
     *         description: Total, online, and offline counts
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/SummaryResponse'
     */
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
    /**
     * @openapi
     * /api/monitoredentities:
     *   get:
     *     tags: [Monitoring]
     *     summary: List all monitored entities
     *     responses:
     *       '200':
     *         description: Array of monitored entities
     *         content:
     *           application/json:
     *             schema:
     *               type: array
     *               items:
     *                 $ref: '#/components/schemas/MonitoredEntity'
     */
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

    /**
     * @openapi
     * /api/monitoredentity/{name}:
     *   get:
     *     tags: [Monitoring]
     *     summary: Get a monitored entity by name
     *     parameters:
     *       - in: path
     *         name: name
     *         required: true
     *         schema:
     *           type: string
     *         example: Router
     *     responses:
     *       '200':
     *         description: Monitored entity details
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/MonitoredEntity'
     *       '404':
     *         description: Entity not found
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/ErrorResponse'
     */
    server.on("^\\/api\\/monitoredentity\\/([^\\/]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String name = urlDecode(request->pathArg(0));
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

    /**
     * @openapi
     * /api/monitoredentity/{name}:
     *   delete:
     *     tags: [Monitoring]
     *     summary: Delete a monitored entity by name
     *     parameters:
     *       - in: path
     *         name: name
     *         required: true
     *         schema:
     *           type: string
     *         example: Router
     *     responses:
     *       '200':
     *         description: Entity deleted
     *         content:
     *           application/json:
     *             schema:
     *               type: object
     *               properties:
     *                 status:
     *                   type: string
     *                   example: deleted
     *       '404':
     *         description: Entity not found
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/ErrorResponse'
     */
    server.on("^\\/api\\/monitoredentity\\/([^\\/]+)$", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!isConfigAuthorized(request)) {
            request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            return;
        }
        String name = urlDecode(request->pathArg(0));
        
        if (monitoredEntityRepository.removeEntity(name)) {
            wsService.broadcastEntityDeleted(name);
            wsService.broadcastSummary();
            request->send(200, "application/json", "{\"status\":\"deleted\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"Not found\"}");
        }
    });

    /**
     * @openapi
     * /api/monitoredentity:
     *   post:
     *     tags: [Monitoring]
     *     summary: Add a new monitored entity
     *     requestBody:
     *       required: true
     *       content:
     *         application/json:
     *           schema:
     *             $ref: '#/components/schemas/MonitoredEntityInput'
     *     responses:
     *       '201':
     *         description: Entity created
     *         content:
     *           application/json:
     *             schema:
     *               type: object
     *               properties:
     *                 status:
     *                   type: string
     *                   example: created
     *       '400':
     *         description: Invalid JSON or missing required fields
     *       '409':
     *         description: Entity with this name already exists
     */
    server.on("/api/monitoredentity", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!isConfigAuthorized(request)) {
            if (index == 0) {
                request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            }
            return;
        }

        if (!WebSecurity::isJsonContentType(request)) {
            if (index == 0) {
                request->send(415, "application/json", "{\"error\":\"Unsupported Media Type: application/json required\"}");
            }
            return;
        }

        if (total > MAX_ENTITY_BODY_SIZE) {
            if (index == 0) {
                request->send(413, "application/json", "{\"error\":\"Payload Too Large\"}");
            }
            return;
        }

        if (index == 0) {
            request->_tempObject = new String("");
            request->onDisconnect([request]() {
                if (request->_tempObject) {
                    delete (String*)request->_tempObject;
                    request->_tempObject = nullptr;
                }
            });
        }
        String* buffer = (String*)request->_tempObject;
        if (buffer) {
            for (size_t i = 0; i < len; i++) *buffer += (char)data[i];
        }

        if (index + len == total) {
            JsonDocument doc(&psramAllocator);
            deserializeJson(doc, *buffer);
            delete buffer;
            request->_tempObject = nullptr;
            
            MonitoredEntity newEntity;
            if (newEntity.fromJsonObject(doc.as<JsonObject>()) && newEntity.isValidForUserInput()) {
                if (!monitoredEntityRepository.hasEntity(newEntity.getName())) {
                    monitoredEntityRepository.addEntity(newEntity);
                    wsService.broadcastEntityUpdate(newEntity);
                    wsService.broadcastSummary();
                    request->send(201, "application/json", "{\"status\":\"created\"}");
                } else {
                    request->send(409, "application/json", "{\"error\":\"Already exists\"}");
                }
            } else {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON, name or address\"}");
            }
        }
    });

    /**
     * @openapi
     * /api/monitoredentity/{name}:
     *   put:
     *     tags: [Monitoring]
     *     summary: Update an existing monitored entity
     *     parameters:
     *       - in: path
     *         name: name
     *         required: true
     *         schema:
     *           type: string
     *         example: Router
     *     requestBody:
     *       required: true
     *       content:
     *         application/json:
     *           schema:
     *             $ref: '#/components/schemas/MonitoredEntityInput'
     *     responses:
     *       '200':
     *         description: Entity updated
     *         content:
     *           application/json:
     *             schema:
     *               type: object
     *               properties:
     *                 status:
     *                   type: string
     *                   example: updated
     *       '400':
     *         description: Invalid JSON
     *       '404':
     *         description: Entity not found
     */
    server.on("^\\/api\\/monitoredentity\\/([^\\/]+)$", HTTP_PUT, [](AsyncWebServerRequest *request) {}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!isConfigAuthorized(request)) {
            if (index == 0) {
                request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Forbidden\"}");
            }
            return;
        }

        if (!WebSecurity::isJsonContentType(request)) {
            if (index == 0) {
                request->send(415, "application/json", "{\"error\":\"Unsupported Media Type: application/json required\"}");
            }
            return;
        }

        if (total > MAX_ENTITY_BODY_SIZE) {
            if (index == 0) {
                request->send(413, "application/json", "{\"error\":\"Payload Too Large\"}");
            }
            return;
        }

        if (index == 0) {
            request->_tempObject = new String("");
            request->onDisconnect([request]() {
                if (request->_tempObject) {
                    delete (String*)request->_tempObject;
                    request->_tempObject = nullptr;
                }
            });
        }
        String* buffer = (String*)request->_tempObject;
        if (buffer) {
            for (size_t i = 0; i < len; i++) *buffer += (char)data[i];
        }

        if (index + len == total) {
            String oldName = urlDecode(request->pathArg(0));

            JsonDocument doc(&psramAllocator);
            deserializeJson(doc, *buffer);
            delete buffer;
            request->_tempObject = nullptr;
            
            MonitoredEntity updatedData;
            if (updatedData.fromJsonObject(doc.as<JsonObject>()) && updatedData.isValidForUserInput()) {
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
                request->send(400, "application/json", "{\"error\":\"Invalid JSON, name or address\"}");
            }
        }
    });
}

void Api::registerSystemInformationEndpoints() {
    /**
     * @openapi
     * /api/health:
     *   get:
     *     tags: [System]
     *     summary: Health check endpoint
     *     responses:
     *       '200':
     *         description: Device is up and responding
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/HealthResponse'
     */
    server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        Logger.debug(MAINLOG, "Health check request");
        request->send(200, "application/json", "{\"status\":\"UP\"}");
    });

    /**
     * @openapi
     * /api/sysinfo/details:
     *   get:
     *     tags: [System]
     *     summary: Get detailed device, memory, storage, network, and firmware diagnostics
     *     responses:
     *       '200':
     *         description: Detailed system information
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/DetailedSysinfo'
     */
    server.on("/api/sysinfo/details", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncJsonResponse *response = new AsyncJsonResponse(false);
        JsonObject doc = response->getRoot().to<JsonObject>();
        SystemInfo::fillDetailedSysinfo(doc);
        response->setLength();
        request->send(response);
    });

    /**
     * @openapi
     * /api/sysinfo:
     *   get:
     *     tags: [System]
     *     summary: Get basic system metrics (heap, RSSI, uptime, temperature, version)
     *     responses:
     *       '200':
     *         description: Basic system information
     *         content:
     *           application/json:
     *             schema:
     *               $ref: '#/components/schemas/BasicSysinfo'
     */
    server.on("/api/sysinfo", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncJsonResponse *response = new AsyncJsonResponse(false);
        JsonObject doc = response->getRoot().to<JsonObject>();
        SystemInfo::fillBasicSysinfo(doc);
        response->setLength();
        request->send(response);
    });
}
