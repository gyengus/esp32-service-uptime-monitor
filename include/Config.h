#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <Arduino.h>
#include <ArduinoJson.h>

#define WIFI_AP_SSID "ESP32-Service-Uptime-Monitor"
#define WIFI_AP_PASSWORD "29965225"
#define DEFAULT_HOSTNAME "esp32_service_uptime_monitor"
#define DEFAULT_SNTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE_OFFSET_HOURS 1
#define DEFAULT_CHECK_INTERVAL_SECONDS 60
#define DEFAULT_LOG_LEVEL 8 // ELOG_LEVEL_DEBUG

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

#define BOOT_BTN_PIN 0
#define LED_PIN 2
#define RGB_LED_PIN 48
#define SD_CMD_PIN 38
#define SD_DATA_PIN 40
#define SD_CLK_PIN 39

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