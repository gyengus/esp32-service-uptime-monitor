#include <Arduino.h>
#include <Elog.h>
#include <Wire.h>
#include <WiFi.h>
#include <math.h>
#include <ESPmDNS.h>
#include "esp_sntp.h"
#include "esp_mac.h"
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "driver/temperature_sensor.h"
#include <Ticker.h>
#include <WiFiUdp.h>

#include "main.h"
#include "version.h"
#include "globals.h"
#include "Storage.h"
#include "Config.h"
#include "NetworkContext.h"
#include "Api.h"
#include "LedService.h"
#include "ImprovService.h"

#ifndef DISABLE_FILE_MANAGER
#include "FileManager.h"
#endif
#include "WebArchiveExtractor.h"
#include "MonitoringService.h"
#include "MonitoredEntityRepository.h"
#include "WsService.h"
#include "WsLogStream.h"
#include "esp_ota_ops.h"

Ticker sntpSyncTicker;
Ticker restartTicker;

DNSServer dnsServer;
AsyncWebServer server(80);
Api api = Api(server);
#ifndef DISABLE_FILE_MANAGER
FileManager fileManager(storage.getFS(), WEB_UI_PATH);
#endif

volatile bool isConfigFunctionsEnabled = false;
bool isAPMode = false;
volatile bool shouldRestart = false;
volatile bool isOtaUpdating = false;
unsigned long configModeLastActivity = 0;

void resetConfigModeActivity() {
    configModeLastActivity = millis();
}

TaskHandle_t MonitoringTaskHandle = NULL;
#ifdef HAS_RGB_LED
TaskHandle_t MonitoringLEDTaskHandle = NULL;
TaskHandle_t APModeLEDTaskHandle = NULL;
#endif
temperature_sensor_handle_t temp_sensor = NULL;

void setup() {
#ifdef HAS_RGB_LED
    ledService.init();
#endif
#ifdef HAS_STATUS_LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
#endif
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

    #ifdef MONITOR_SPEED
        Serial.begin(MONITOR_SPEED);
    #else
        Serial.begin(115200);
    #endif //MONITOR_SPEED

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    #ifdef MONITOR_SPEED
        Serial0.begin(MONITOR_SPEED);
    #else
        Serial0.begin(115200);
    #endif
#endif
    delay(5000);

    Logger.registerSerial(MAINLOG, MIN_LOG_LEVEL, "LOG", Serial);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Logger.registerSerial(MAINLOG, MIN_LOG_LEVEL, "LOG", Serial0);
#endif
    Logger.registerSerial(MAINLOG, MIN_LOG_LEVEL, "LOG", wsLogStream);

    networkContext.setLogger([](const char* level, const char* msg) {
        if (strcmp(level, "ERROR") == 0) {
            Logger.error(MAINLOG, "%s", msg);
        } else if (strcmp(level, "WARNING") == 0) {
            Logger.warning(MAINLOG, "%s", msg);
        } else if (strcmp(level, "DEBUG") == 0) {
            Logger.debug(MAINLOG, "%s", msg);
        } else {
            Logger.info(MAINLOG, "%s", msg);
        }
    });

#ifndef DISABLE_FILE_MANAGER
    fileManager.setLogger([](const char* level, const char* msg) {
        if (strcasecmp(level, "error") == 0) {
            Logger.error(MAINLOG, "%s", msg);
        } else if (strcasecmp(level, "warning") == 0) {
            Logger.warning(MAINLOG, "%s", msg);
        } else if (strcasecmp(level, "debug") == 0) {
            Logger.debug(MAINLOG, "%s", msg);
        } else {
            Logger.info(MAINLOG, "%s", msg);
        }
    });
    fileManager.setAvailabilityCheck([]() {
        return storage.isAvailable();
    });
    fileManager.setAuthCheck([](AsyncWebServerRequest *request) {
        if (isConfigFunctionsEnabled || isAPMode) {
            if (isConfigFunctionsEnabled) {
                resetConfigModeActivity();
            }
            return true;
        }
        return false;
    });
#endif

    Logger.info(MAINLOG, "ESP32 Service Uptime Monitor v%s", FIRMWARE_VERSION);
    Logger.info(MAINLOG, "Commit hash: %s, Build date: %s", COMMIT_HASH, BUILD_TIMESTAMP);

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        Logger.info(MAINLOG, "Running partition: %s (address: 0x%06x)", running->label, running->address);
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            Logger.info(MAINLOG, "OTA state: 0x%02x", ota_state);
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY || ota_state == ESP_OTA_IMG_NEW) {
                Logger.info(MAINLOG, "OTA update successful, confirming new firmware (canceling rollback)");
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }
    }

    if (psramInit()) {
        Logger.info(MAINLOG, "PSRAM initialized, size: %d bytes, free: %d bytes", ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Logger.error(MAINLOG, "PSRAM not found");
    }

    uint8_t sta_mac[6];
    esp_read_mac(sta_mac, ESP_MAC_WIFI_STA); 
    Logger.info(MAINLOG, "Mac: %02X:%02X:%02X:%02X:%02X:%02X", sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);

#ifdef HAS_TEMPERATURE_SENSOR
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t error = temperature_sensor_install(&temp_sensor_config, &temp_sensor);
    if (error == ESP_OK) {
        temperature_sensor_enable(temp_sensor);
        Logger.info(MAINLOG, "Temperature sensor initialized");
        float tsens_out = 0.0;
        if (temperature_sensor_get_celsius(temp_sensor, &tsens_out) == ESP_OK) {
            Logger.info(MAINLOG, "Current temperature: %.2f °C", tsens_out);
        }
    } else {
        temp_sensor = NULL;
        Logger.info(MAINLOG, "Internal temperature sensor not supported on this chip/board");
    }
#else
    temp_sensor = NULL;
    Logger.info(MAINLOG, "Internal temperature sensor not supported on this chip");
#endif

    if (!storage.init()) {
        errorMode();
    }

    const String versionFilePath = String(WEB_UI_PATH) + "/.version";
    const String currentVersion = String(FIRMWARE_VERSION) + "-" + String(COMMIT_HASH);
    bool shouldExtractWeb = false;
    String storedVersion = "";

    if (!storage.fileExists(String(WEB_UI_PATH) + "/index.html") || 
        !storage.fileExists(String(WEB_UI_PATH) + "/config.html") ||
#ifndef DISABLE_FILE_MANAGER
        !storage.fileExists(String(WEB_UI_PATH) + "/fileManager.html") ||
#endif
        !storage.fileExists(versionFilePath)) {
        shouldExtractWeb = true;
    } else {
        storedVersion = storage.readFile(versionFilePath);
        storedVersion.trim();
        if (storedVersion != currentVersion) {
            shouldExtractWeb = true;
        }
    }

    if (shouldExtractWeb) {
        if (storedVersion.length() > 0 && storedVersion != currentVersion) {
            Logger.info(MAINLOG, "Web UI version mismatch (installed: %s, firmware: %s), updating web assets...",
                        storedVersion.c_str(), currentVersion.c_str());
        } else {
            Logger.info(MAINLOG, "Web UI assets missing from storage, extracting embedded web archive...");
        }

        if (WebArchiveExtractor::extractEmbeddedArchive(WEB_UI_PATH)) {
            storage.writeFile(versionFilePath, currentVersion);
            Logger.info(MAINLOG, "Embedded Web UI successfully extracted to %s (version: %s)", WEB_UI_PATH, currentVersion.c_str());
        } else {
            Logger.error(MAINLOG, "Failed to extract embedded Web UI archive");
        }
    } else {
        Logger.info(MAINLOG, "Web UI up to date (version: %s)", currentVersion.c_str());
    }

#ifdef DISABLE_FILE_MANAGER
    if (storage.fileExists(String(WEB_UI_PATH) + "/fileManager.html")) {
        storage.deleteFile(String(WEB_UI_PATH) + "/fileManager.html");
        Logger.info(MAINLOG, "File Manager disabled: removed %s/fileManager.html from storage", WEB_UI_PATH);
    }
#endif

    if (!storage.fileExists(CONFIG_FILE_PATH)) {
        Logger.warning(MAINLOG, "Config file not found, entering configuration mode");
        configurationMode();
    } else {
        initNormalMode();
    }

    commonWebServerSettings();
    server.begin();

#ifdef HAS_STATUS_LED
    digitalWrite(LED_PIN, LOW);
#endif
}

void loop() {
    handleButton();
    if (!isOtaUpdating) {
        improvService.handleSerial();
    }

    if (isConfigFunctionsEnabled && !isAPMode) {
        if (getConfigModeRemainingSeconds() == 0) {
            isConfigFunctionsEnabled = false;
            Logger.info(MAINLOG, "Configuration mode timed out after 10 minutes of inactivity");
            wsService.broadcastSysinfo();
        }
    }

    if (shouldRestart) {
        static bool restartScheduled = false;
        if (!restartScheduled) {
            restartScheduled = true;
            if (!isOtaUpdating) {
                storage.unmount();
            }
            restartTicker.once_ms(1500, []() {
                ESP.restart();
            });
        }
        return;
    }

    if (isAPMode) {
        dnsServer.processNextRequest();
    } else if (!isOtaUpdating) {
        wsService.cleanupClients();
        wsService.processEvents();

        static unsigned long lastWsSysinfo = 0;
        if (millis() - lastWsSysinfo >= WS_SYSINFO_INTERVAL) {
            lastWsSysinfo = millis();
            if (wsService.hasActiveClients()) {
                wsService.broadcastSysinfo();
            }
        }

#if defined(MEM_STATS_LOGGING) && !defined(DISABLE_MEM_STATS_LOGGING)
        static unsigned long lastMemCheck = 0;
        if (millis() - lastMemCheck > 10000) {
            lastMemCheck = millis();
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "Free Heap: %u B, Max Alloc: %u B, Free PSRAM: %u B, Max Free Block: %d B", 
                ESP.getFreeHeap(), 
                ESP.getMaxAllocHeap(), 
                ESP.getFreePsram(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            Logger.debug(MAINLOG, "%s", buffer);
        }
#endif
        networkContext.checkConnection();
    }
}

void configurationMode() {
    isAPMode = true;

    Logger.info(MAINLOG, "Starting Setup AP (Open) - SSID: %s", WIFI_AP_SSID);
    networkContext.startAPMode(WIFI_AP_SSID, "", 8, 4);

    dnsServer.start();

    // start web server for configuration
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String path = String(WEB_UI_PATH) + "/config.html";
        if (!storage.fileExists(path)) {
            Logger.error(MAINLOG, "Web UI missing from storage: %s", path.c_str());
            request->send(500, "text/plain", "Web UI missing from storage. Please re-flash or upload web.zip.");
            return;
        }
        request->send(storage.getFS(), path, "text/html");
    });

#ifdef HAS_RGB_LED
    xTaskCreate(
        apModeLEDTask,
        "APModeLEDTask",
        4096,
        NULL,
        1,
        &APModeLEDTaskHandle
    );
#endif
}

void initNormalMode() {
    Logger.setSerialLogLevel(MAINLOG, config.getConfig().logLevel, Serial);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Logger.setSerialLogLevel(MAINLOG, config.getConfig().logLevel, Serial0);
#endif
    Logger.setSerialLogLevel(MAINLOG, config.getConfig().logLevel, wsLogStream);
    Logger.info(MAINLOG, "Config file found, loading...");
    if (!config.loadConfig()) {
        errorMode();
    }
    networkContext.connect(config.getConfig().wifiSSID, config.getConfig().wifiPassword, config.getConfig().hostname, WIFI_CONNECT_TIMEOUT);

    // web server settings
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path = String(WEB_UI_PATH) + "/index.html";
        if (!storage.fileExists(path)) {
            Logger.error(MAINLOG, "Web UI missing from storage: %s", path.c_str());
            request->send(500, "text/plain", "Web UI missing from storage. Please re-flash or upload web.zip.");
            return;
        }
        request->send(storage.getFS(), path, "text/html");
    });
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->redirect("/");
            return;
        }
        String path = String(WEB_UI_PATH) + "/config.html";
        if (!storage.fileExists(path)) {
            Logger.error(MAINLOG, "Web UI missing from storage: %s", path.c_str());
            request->send(500, "text/plain", "Web UI missing from storage. Please re-flash or upload web.zip.");
            return;
        }
        request->send(storage.getFS(), path, "text/html");
    });

    // mDNS
    if (MDNS.begin(config.getConfig().hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "name", config.getConfig().hostname.c_str());
        Logger.info(MAINLOG, "MDNS responder started: http://%s.local/", config.getConfig().hostname.c_str());
    } else {
        Logger.error(MAINLOG, "Error setting up MDNS responder!");
    }

    // SNTP
    if (!config.getConfig().sntpServer.isEmpty()) {
        char posixString[64];
        snprintf(posixString, sizeof(posixString), "CET%dCEST,M3.5.0,M10.5.0", -config.getConfig().offsetHours);
        setenv("TZ", posixString, 1);
        tzset();
        onSntpSyncSchedule();
        sntpSyncTicker.attach(SNTP_SYNC_INTERVAL, onSntpSyncSchedule);
    }

    wsService.init(server);
    monitoringService.init();


#ifdef HAS_RGB_LED
    xTaskCreate(
        monitoringLEDTask,
        "MonitoringLEDTask",
        4096,
        NULL,
        1,
        &MonitoringLEDTaskHandle
    );
#endif

#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(
        monitoringTaskLoop,
        "MonitoringTask",
        8192,
        NULL,
        5,
        &MonitoringTaskHandle
    );
#else
    xTaskCreatePinnedToCore(
        monitoringTaskLoop,
        "MonitoringTask",
        8192,
        NULL,
        5,
        &MonitoringTaskHandle,
        0
    );
#endif
}

void commonWebServerSettings() {
    DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");
    DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
    DefaultHeaders::Instance().addHeader("Referrer-Policy", "no-referrer");
    DefaultHeaders::Instance().addHeader("Content-Security-Policy", "default-src 'self'; img-src 'self' data: https://cdn.jsdelivr.net; style-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; font-src 'self' https://cdn.jsdelivr.net; script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; connect-src 'self' ws: wss: https://cdn.jsdelivr.net;");

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(storage.getFS(), (String(WEB_UI_PATH) + "/static/favicon.ico"), "image/x-icon");
    });
    server.on("/api/docs", HTTP_GET, [](AsyncWebServerRequest *request){
        String path = String(WEB_UI_PATH) + "/api-docs.html";
        if (!storage.fileExists(path)) {
            request->send(404, "text/plain", "API documentation not found in storage");
            return;
        }
        request->send(storage.getFS(), path, "text/html");
    });
    server.on("/swagger", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/api/docs");
    });
    server.on("/api/openapi.yaml", HTTP_GET, [](AsyncWebServerRequest *request){
        String path = String(WEB_UI_PATH) + "/openapi.yaml";
        if (!storage.fileExists(path)) {
            request->send(404, "text/plain", "OpenAPI specification not found in storage");
            return;
        }
        request->send(storage.getFS(), path, "text/yaml; charset=utf-8");
    });
    server.on("/license", HTTP_GET, [](AsyncWebServerRequest *request){
        String path = String(WEB_UI_PATH) + "/LICENSE";
        if (!storage.fileExists(path)) {
            request->send(404, "text/plain", "License file not found in storage");
            return;
        }
        request->send(storage.getFS(), path, "text/plain; charset=utf-8");
    });
    server.serveStatic("/static", storage.getFS(), (String(WEB_UI_PATH) + "/static").c_str());
    api.registerEndpoints();
    server.onNotFound([](AsyncWebServerRequest *request){
        if (request->method() == HTTP_OPTIONS) {
            request->send(204);
            return;
        }

        String host = request->host();
        String clientIP = request->client() ? request->client()->remoteIP().toString() : "unknown";
        Logger.warning(MAINLOG, "404 Not Found: %s %s%s (Client: %s)",
            request->methodToString(), host.c_str(), request->url().c_str(), clientIP.c_str());

        if (isAPMode && !host.equals(networkContext.getIP())) {
            Logger.info(MAINLOG, "Redirecting to captive portal");
            request->redirect("http://192.168.4.1/");
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
#ifndef DISABLE_FILE_MANAGER
    fileManager.registerHandlers(server);
#endif
}

void onSntpSyncSchedule() {
    WiFiUDP ntpUDP;
    String host = config.getConfig().sntpServer;
    IPAddress timeServerIP;
    uint8_t packetBuffer[48];

    if (!WiFi.hostByName(host.c_str(), timeServerIP)) {
        Logger.error(MAINLOG, "DNS lookup failed for: %s", host.c_str());
        timeServerIP.fromString(FALLBACK_SNTP_SERVER_IP); 
        Logger.info(MAINLOG, "Falling back to Google NTP IP: %s", FALLBACK_SNTP_SERVER_IP);
    }

    if (ntpUDP.begin(random(49152, 65535))) {
        memset(packetBuffer, 0, 48);
        packetBuffer[0] = 0b00100011;   // LI = 0, Version = 4, Mode = 3 (Client)
        packetBuffer[1] = 0;            // Stratum, or type of clock
        packetBuffer[2] = 6;            // Polling Interval
        packetBuffer[3] = 0xEC;         // Peer Clock Precision

        ntpUDP.beginPacket(timeServerIP, 123);
        ntpUDP.write(packetBuffer, 48);
        ntpUDP.endPacket();

        uint32_t start = millis();
        bool received = false;
        while (millis() - start < 3000) {
            int size = ntpUDP.parsePacket();
            if (size >= 48) {
                received = true;
                break;
            }
            delay(50);
        }

        if (received) {
            ntpUDP.read(packetBuffer, 48);
    
            uint32_t secsSince1900 = (uint32_t)packetBuffer[40] << 24 | (uint32_t)packetBuffer[41] << 16 |
                                    (uint32_t)packetBuffer[42] << 8  | (uint32_t)packetBuffer[43];
            
            uint32_t fraction = (uint32_t)packetBuffer[44] << 24 | (uint32_t)packetBuffer[45] << 16 |
                                (uint32_t)packetBuffer[46] << 8  | (uint32_t)packetBuffer[47];
            uint32_t ntp_ms = (uint64_t)fraction * 1000 >> 32;

            const uint32_t seventyYears = 2208988800UL;
            time_t ntp_now_s = secsSince1900 - seventyYears;

            struct timeval old_tv;
            gettimeofday(&old_tv, NULL);
            struct timeval new_tv = { .tv_sec = ntp_now_s, .tv_usec = (suseconds_t)(ntp_ms * 1000) };
            settimeofday(&new_tv, NULL);

            struct tm timeinfo;
            localtime_r(&ntp_now_s, &timeinfo);
            char timeStr[25];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            char buffer[80];
            if (old_tv.tv_sec < 1577836800L) { 
                snprintf(buffer, sizeof(buffer), "Time synchronised, time: %s", timeStr);
            } else {
                int64_t drift_ms = ((int64_t)ntp_now_s - old_tv.tv_sec) * 1000 + (long)ntp_ms - (old_tv.tv_usec / 1000);
                snprintf(buffer, sizeof(buffer), "Time synchronised, time: %s (drift: %lld ms)", timeStr, drift_ms);
            }
            Logger.info(MAINLOG, "%s", buffer);
        } else {
            Logger.error(MAINLOG, "Time syncronisation failed: No response from server.");
        }
        ntpUDP.stop();
    }
}

void errorMode() {
    while(true) {
        ledService.updateErrorLed();
        delay(20);
    }
}

void handleButton() {
    static unsigned long buttonPressStart = 0;
    static bool buttonWasPressed = false;
    static bool resetTriggered = false;

    if (millis() < 1000) return;

    bool isPressed = (digitalRead(BOOT_BTN_PIN) == LOW);

    if (isPressed) {
        if (!buttonWasPressed) {
            buttonWasPressed = true;
            buttonPressStart = millis();
            resetTriggered = false;
        } else if (!resetTriggered && (millis() - buttonPressStart >= 10000)) {
            resetTriggered = true;
            Logger.warning(MAINLOG, "BOOT button held for 10s - performing factory reset...");
            storage.deleteFile(CONFIG_FILE_PATH);
            storage.deleteFile(ENTITIES_FILE_PATH);
            shouldRestart = true;
        }
    } else {
        if (buttonWasPressed) {
            unsigned long duration = millis() - buttonPressStart;
            buttonWasPressed = false;
            if (!resetTriggered && duration >= 50 && duration < 5000) {
                isConfigFunctionsEnabled = !isConfigFunctionsEnabled;
                if (isConfigFunctionsEnabled) {
                    resetConfigModeActivity();
                }
                Logger.info(MAINLOG, "BOOT button pressed, configuration functions %s",
                            isConfigFunctionsEnabled ? "enabled" : "disabled");
                wsService.broadcastSysinfo();
            }
        }
    }
}

void monitoringTaskLoop(void * pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    Logger.info(MAINLOG, "Started monitoring task on core: %d", xPortGetCoreID());
    for(;;) {
        if (isOtaUpdating) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (WiFi.status() == WL_CONNECTED) {
#ifdef HAS_STATUS_LED
            digitalWrite(LED_PIN, HIGH);
#endif
            monitoringService.checkAll();
#ifdef HAS_STATUS_LED
            digitalWrite(LED_PIN, LOW);
#endif
        } else {
            Logger.warning(MAINLOG, "Skipping monitoring cycle, WiFi not connected");
        }
        vTaskDelay(config.getConfig().checkInterval * 1000 / portTICK_PERIOD_MS);
    }
}

#ifdef HAS_RGB_LED
void monitoringLEDTask(void * pvParameters) {
    while(1) {
        ledService.updateMonitoringLed(monitoringService.getOfflineCount());
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void apModeLEDTask(void * pvParameters) {
    while(1) {
        ledService.updateApModeLed();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif
