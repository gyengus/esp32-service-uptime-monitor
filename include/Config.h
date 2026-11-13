#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <Arduino.h>
#include <ArduinoJson.h>

#define WIFI_AP_SSID "ESP32-Service-Uptime-Monitor"
#define WIFI_AP_PASSWORD "29965225"
#define DEFAULT_HOSTNAME "esp32-service-uptime-monitor"
#define DEFAULT_SNTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE_OFFSET_HOURS 1
#define DEFAULT_CHECK_INTERVAL_SECONDS 60
#define DEFAULT_LED_BRIGHTNESS 50
#include <Elog.h>

#ifdef MIN_LOG_LEVEL
#define DEFAULT_LOG_LEVEL MIN_LOG_LEVEL
#else
#define DEFAULT_LOG_LEVEL ELOG_LEVEL_DEBUG
#endif

#define CONFIG_FILE_PATH "/config.json"
#define WEB_UI_PATH "/web"
#define ENTITIES_FILE_PATH "/monitored_entities.json"

#define MAINLOG 0

#define WIFI_CONNECT_TIMEOUT 10 // seconds
#define SNTP_SYNC_INTERVAL 15 * 60 // seconds
#define HTTP_REQUEST_TIMEOUT 5000 // milliseconds
#define WS_SYSINFO_INTERVAL 2000 // milliseconds

#define FALLBACK_SNTP_SERVER_IP "216.239.35.0" // Google NTP

#define HTTP_REQUEST_RETRIES 3

// ==============================================================================
// Feature Flags
// ==============================================================================
// Define DISABLE_FILE_MANAGER to completely strip File Manager from the firmware.
// When defined, all File Manager routes (/fileManager, /list, /upload, /delete,
// /mkdir, /download) are omitted, no code is compiled in, and fileManager.html is
// excluded from web.zip / flash storage.
// Can also be set in platformio.ini build_flags: -D DISABLE_FILE_MANAGER
// or via command-line: DISABLE_FILE_MANAGER=1 pio run
// #define DISABLE_FILE_MANAGER

// Define DISABLE_MEM_STATS_LOGGING to disable periodic heap/PSRAM memory statistics logging.
// Can also be set in platformio.ini build_flags: -D DISABLE_MEM_STATS_LOGGING
// or via command-line: DISABLE_MEM_STATS_LOGGING=1 pio run
// #define DISABLE_MEM_STATS_LOGGING

// Define MIN_LOG_LEVEL to override compilation log level (e.g. ELOG_LEVEL_INFO).
// Automatically set to ELOG_LEVEL_INFO when building with RELEASE=1.
// Can also be set in platformio.ini build_flags: -D MIN_LOG_LEVEL=ELOG_LEVEL_INFO
// or via command-line: MIN_LOG_LEVEL=INFO pio run
// #define MIN_LOG_LEVEL ELOG_LEVEL_INFO

#include "devices.h"

// Discord embed colors
#define DISCORD_COLOR_BLUE 3447003  // #3498DB
#define DISCORD_COLOR_GREEN 1677332  // #198754
#define DISCORD_COLOR_RED   14431557  // #DC3545

typedef struct {
    String wifiSSID;
    String wifiPassword;
    String sntpServer = DEFAULT_SNTP_SERVER;
    int8_t offsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
    uint8_t checkInterval = DEFAULT_CHECK_INTERVAL_SECONDS;
    String discordWebhookURL;
    String hostname = DEFAULT_HOSTNAME;
    uint8_t logLevel = DEFAULT_LOG_LEVEL;
    uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;
    String otaToken;
} config_t;

class Config {
public:
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    static Config& getInstance();

    bool loadConfig();
    bool saveConfig(const config_t& config);
    bool updateLogLevel(uint8_t newLevel);
    config_t getConfig();
    uint8_t getLedBrightness() const { return currentConfig.ledBrightness; }
    String getOtaToken() const { return currentConfig.otaToken; }
    void fillJson(JsonObject& doc) const;
    void fillJson(JsonObject& doc, config_t config) const;
    config_t fromJsonObject(const JsonObject& doc);
    String toJson();
    String toJson(config_t config);
    config_t fromJson(String json);

    static const char* logLevelToString(uint8_t level);
    static uint8_t stringToLogLevel(const char* str, uint8_t defaultLevel = DEFAULT_LOG_LEVEL);
private:
    config_t currentConfig;
    Config() { }
};

extern Config& config;

#endif // _CONFIG_H_