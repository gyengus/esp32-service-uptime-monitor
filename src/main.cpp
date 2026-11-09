#include <Arduino.h>
#include <Elog.h>
#include <Wire.h>
#include <SD_MMC.h>
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

#include "fileManager.h"
#include "MonitoringService.h"
#include "MonitoredEntityRepository.h"
#include "WsService.h"
#include "WsLogStream.h"
#include <ElegantOTA.h>
#include "esp_ota_ops.h"

Ticker sntpSyncTicker;
Ticker restartTicker;

DNSServer dnsServer;
AsyncWebServer server(80);
Api api = Api(server);

volatile bool isConfigFunctionsEnabled = false;
bool isAPMode = false;
volatile bool buttonEventPending = false;
volatile bool shouldRestart = false;
volatile bool isOtaUpdating = false;

TaskHandle_t MonitoringTaskHandle = NULL;
TaskHandle_t MonitoringLEDTaskHandle = NULL;
TaskHandle_t APModeLEDTaskHandle = NULL;
temperature_sensor_handle_t temp_sensor = NULL;

void setup() {
    rgbLedWrite(RGB_LED_PIN, 0, 0, 0);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOOT_BTN_PIN), handleButtonInterrupt, FALLING);

    #ifdef MONITOR_SPEED
        Serial.begin(MONITOR_SPEED);
    #else
        Serial.begin(115200);
    #endif //MONITOR_SPEED
    delay(5000);

    Logger.registerSerial(MAINLOG, MIN_LOG_LEVEL, "LOG", Serial);
    Logger.registerSerial(MAINLOG, MIN_LOG_LEVEL, "LOG", wsLogStream);

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

    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t error = temperature_sensor_install(&temp_sensor_config, &temp_sensor);
    if (error == ESP_OK) {
        temperature_sensor_enable(temp_sensor);
        Logger.info(MAINLOG, "Temperature sensor initialized");
    } else {
        Logger.error(MAINLOG, "Failed to initialize temperature sensor: %s", esp_err_to_name(error));
    }

    float tsens_out = 0.0;
    error = temperature_sensor_get_celsius(temp_sensor, &tsens_out);
    if (error == ESP_OK) {
        Logger.info(MAINLOG, "Current temperature: %.2f °C", tsens_out);
    } else {
        Logger.error(MAINLOG, "Failed to read temperature sensor: %s", esp_err_to_name(error));
    }

    if (!storage.init()) {
        errorMode();
    }

    if (!storage.fileExists(CONFIG_FILE_PATH)) {
        Logger.warning(MAINLOG, "Config file not found, entering configuration mode");
        configurationMode();
    } else {
        initNormalMode();
    }

    commonWebServerSettings();
    server.begin();

    digitalWrite(LED_PIN, LOW);
}

void loop() {
    ElegantOTA.loop();
    if (buttonEventPending) {
        buttonEventPending = false;
        Logger.debug(MAINLOG, "BOOT button pressed, configuration functions %s", isConfigFunctionsEnabled ? "enabled" : "disabled");
        wsService.broadcastSysinfo();
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

#ifdef MEM_STATS_LOGGING
        static unsigned long lastMemCheck = 0;
        if (millis() - lastMemCheck > 10000) {
            lastMemCheck = millis();
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "Free Heap: %u B, Max Alloc: %u B, Free PSRAM: %u B, Max Free Block: %d B", 
                ESP.getFreeHeap(), 
                ESP.getMaxAllocHeap(), 
                ESP.getFreePsram(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            Logger.debug(MAINLOG, buffer);
        }
#endif
        if (shouldRestart) {
            storage.unmount();
            restartTicker.once_ms(2000, []() {
                ESP.restart();
            });
        } else if (!isOtaUpdating) {
            networkContext.checkConnection();
        }
    }
}

void configurationMode() {
    isAPMode = true;
    networkContext.startAPMode();

    dnsServer.start();

    // start web server for configuration
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SD_MMC, (String(WEB_UI_PATH) + "/config.html"), "text/html");
    });

    xTaskCreate(
        apModeLEDTask,
        "APModeLEDTask",
        4096,
        NULL,
        1,
        &APModeLEDTaskHandle
    );
}

void initNormalMode() {
    Logger.setSerialLogLevel(MAINLOG, config.getConfig().logLevel, Serial);
    Logger.setSerialLogLevel(MAINLOG, config.getConfig().logLevel, wsLogStream);
    Logger.info(MAINLOG, "Config file found, loading...");
    if (!config.loadConfig()) {
        errorMode();
    }
    networkContext.connect(config.getConfig().wifiSSID, config.getConfig().wifiPassword);

    // web server settings
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SD_MMC, (String(WEB_UI_PATH) + "/index.html"), "text/html");
    });
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!isConfigFunctionsEnabled && !isAPMode) {
            request->send(403, "text/plain", "Forbidden");
            return;
        }
        request->send(SD_MMC, (String(WEB_UI_PATH) + "/config.html"), "text/html");
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


    xTaskCreate(
        monitoringLEDTask,
        "MonitoringLEDTask",
        4096,
        NULL,
        1,
        &MonitoringLEDTaskHandle
    );

    xTaskCreatePinnedToCore(
        monitoringTaskLoop,
        "MonitoringTask",
        8192,
        NULL,
        5,
        &MonitoringTaskHandle,
        0
    );
}

void setupOTA() {
    ElegantOTA.begin(&server);

    ElegantOTA.onStart([]() {
        isOtaUpdating = true;
    });

    ElegantOTA.onEnd([](bool success) {
        if (success) {
            storage.unmount();
        } else {
            isOtaUpdating = false;
        }
    });
}

void commonWebServerSettings() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SD_MMC, (String(WEB_UI_PATH) + "/static/favicon.ico"), "image/x-icon");
    });
    server.serveStatic("/static", SD_MMC, (String(WEB_UI_PATH) + "/static").c_str());
    setupOTA();
    api.registerEndpoints();
        server.onNotFound([](AsyncWebServerRequest *request){
        String host = request->host();
        String clientIP = request->client() ? request->client()->remoteIP().toString() : "unknown";
        Logger.warning(MAINLOG, "404 Not Found: %s%s (Client: %s)", host.c_str(), request->url().c_str(), clientIP.c_str());

        if (isAPMode && !host.equals(networkContext.getIP())) {
            Logger.info(MAINLOG, "Redirecting to captive portal");
            request->redirect("http://192.168.4.1/");
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });
    registerFileHandlers(server);
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
            Logger.info(MAINLOG, buffer);
        } else {
            Logger.error(MAINLOG, "Time syncronisation failed: No response from server.");
        }
        ntpUDP.stop();
    }
}

void errorMode() {
    const int pulseSpeed = 4000; // ms
    const uint8_t maxBrightness = 180; // 0-255
    while(true) {
        float progress = (float)(millis() % pulseSpeed) / pulseSpeed;
        float brightnessFactor = (sin(progress * 2.0 * PI - PI / 2) + 1.0) / 2.0;
        uint8_t currentBrightness = (uint8_t)(brightnessFactor * maxBrightness);
        rgbLedWrite(RGB_LED_PIN, currentBrightness, 0, 0);
    }
}

void IRAM_ATTR handleButtonInterrupt() {
    unsigned long currentTime = millis();
    static unsigned long lastDebounceTime = 0;

    if (currentTime < 1000) {
        return; 
    }

    if ((digitalRead(BOOT_BTN_PIN) == LOW) && (currentTime - lastDebounceTime > 200)) {
        isConfigFunctionsEnabled = !isConfigFunctionsEnabled;
        lastDebounceTime = currentTime;
        buttonEventPending = true;
    }
}

void monitoringTaskLoop(void * pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    Logger.info(MAINLOG, "Started monitoring task on core: %d", xPortGetCoreID);
    for(;;) {
        if (isOtaUpdating) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (WiFi.status() == WL_CONNECTED) {
            digitalWrite(LED_PIN, HIGH);
            monitoringService.checkAll();
            digitalWrite(LED_PIN, LOW);
        } else {
            Logger.warning(MAINLOG, "Skipping monitoring cycle, WiFi not connected");
        }
        vTaskDelay(config.getConfig().checkInterval * 1000 / portTICK_PERIOD_MS);
    }
}

void monitoringLEDTask(void * pvParameters) {
    const int pulseSpeed = 1500; // ms
    const uint8_t maxBrightness = 150;

    while(1) {
        if (monitoringService.getOfflineCount() > 0) {
            float progress = (float)(millis() % pulseSpeed) / pulseSpeed;
            float brightnessFactor = (sin(progress * 2.0 * PI - PI / 2) + 1.0) / 2.0;
            uint8_t currentBrightness = (uint8_t)(brightnessFactor * maxBrightness);
            rgbLedWrite(RGB_LED_PIN, currentBrightness, 0, currentBrightness / 3);
        } else {
            rgbLedWrite(RGB_LED_PIN, 0, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void apModeLEDTask(void * pvParameters) {
    const int pulseSpeed = 2000; // ms
    const uint8_t maxBrightness = 100; // 0-255
    while(1) {
        float progress = (float)(millis() % pulseSpeed) / pulseSpeed;
        float brightnessFactor = (sin(progress * 2.0 * PI - PI / 2) + 1.0) / 2.0;
        uint8_t currentBrightness = (uint8_t)(brightnessFactor * maxBrightness);
        rgbLedWrite(RGB_LED_PIN, currentBrightness, currentBrightness / 2, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
