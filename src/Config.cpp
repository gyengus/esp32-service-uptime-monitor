#include "Config.h"
#include <Elog.h>
#include "Storage.h"

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::loadConfig() {
    String json = storage.readFile(CONFIG_FILE_PATH);

    if (json.length() == 0) {
        Logger.error(MAINLOG, "Config file is empty or could not be read");
        return false;
    }

    currentConfig = fromJson(json);
    return true;
}

bool Config::saveConfig(const config_t& newConfig) {
    currentConfig = newConfig;
    return storage.writeFile(CONFIG_FILE_PATH, toJson(newConfig));
}

bool Config::updateLogLevel(uint8_t newLevel) {
    if (newLevel > 11) newLevel = DEFAULT_LOG_LEVEL;
    currentConfig.logLevel = newLevel;
    return saveConfig(currentConfig);
}

config_t Config::getConfig() {
    return currentConfig;
}

void Config::fillJson(JsonObject& doc) const {
    fillJson(doc, currentConfig);
}

void Config::fillJson(JsonObject& doc, config_t config) const {
    doc["wifiSSID"] = config.wifiSSID;
    doc["wifiPassword"] = config.wifiPassword;
    doc["sntpServer"] = config.sntpServer;
    doc["offsetHours"] = config.offsetHours;
    doc["checkInterval"] = config.checkInterval;
    doc["discordWebhookURL"] = config.discordWebhookURL;
    doc["hostname"] = config.hostname;
    doc["logLevel"] = config.logLevel;
    doc["logLevelName"] = logLevelToString(config.logLevel);
}

String Config::toJson() {
    return toJson(currentConfig);
}

String Config::toJson(config_t config) {
    String jsonString;
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    fillJson(root, config);
    serializeJson(doc, jsonString);
    return jsonString;
}

config_t Config::fromJson(String jsonString) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
        Logger.error(MAINLOG, "Failed to parse JSON: %s", error.c_str());
        return {};
    }
    return fromJsonObject(doc.as<JsonObject>());
}

config_t Config::fromJsonObject(const JsonObject& doc) {
    config_t config;
    config.wifiSSID = doc["wifiSSID"] | "";
    config.wifiPassword = doc["wifiPassword"] | "";
    config.sntpServer = doc["sntpServer"] | DEFAULT_SNTP_SERVER;
    config.offsetHours = doc["offsetHours"] | DEFAULT_TIMEZONE_OFFSET_HOURS;
    config.checkInterval = doc["checkInterval"] | DEFAULT_CHECK_INTERVAL_SECONDS;
    config.discordWebhookURL = doc["discordWebhookURL"] | "";
    config.hostname = doc["hostname"] | DEFAULT_HOSTNAME;

    if (doc["logLevel"].is<uint8_t>() || doc["logLevel"].is<int>()) {
        config.logLevel = doc["logLevel"].as<uint8_t>();
    } else if (doc["logLevel"].is<const char*>()) {
        config.logLevel = stringToLogLevel(doc["logLevel"].as<const char*>());
    } else if (doc["logLevelName"].is<const char*>()) {
        config.logLevel = stringToLogLevel(doc["logLevelName"].as<const char*>());
    } else {
        config.logLevel = DEFAULT_LOG_LEVEL;
    }

    return config;
}

const char* Config::logLevelToString(uint8_t level) {
    switch (level) {
        case 0: return "ALWAYS";
        case 1: return "EMERGENCY";
        case 2: return "ALERT";
        case 3: return "CRITICAL";
        case 4: return "ERROR";
        case 5: return "WARNING";
        case 6: return "NOTICE";
        case 7: return "INFO";
        case 8: return "DEBUG";
        case 9: return "TRACE";
        case 10: return "VERBOSE";
        case 11: return "NOLOG";
        default: return "DEBUG";
    }
}

uint8_t Config::stringToLogLevel(const char* str, uint8_t defaultLevel) {
    if (!str) return defaultLevel;
    if (strcasecmp(str, "ALWAYS") == 0) return 0;
    if (strcasecmp(str, "EMERGENCY") == 0) return 1;
    if (strcasecmp(str, "ALERT") == 0) return 2;
    if (strcasecmp(str, "CRITICAL") == 0) return 3;
    if (strcasecmp(str, "ERROR") == 0) return 4;
    if (strcasecmp(str, "WARNING") == 0 || strcasecmp(str, "WARN") == 0) return 5;
    if (strcasecmp(str, "NOTICE") == 0) return 6;
    if (strcasecmp(str, "INFO") == 0) return 7;
    if (strcasecmp(str, "DEBUG") == 0) return 8;
    if (strcasecmp(str, "TRACE") == 0) return 9;
    if (strcasecmp(str, "VERBOSE") == 0) return 10;
    if (strcasecmp(str, "NOLOG") == 0 || strcasecmp(str, "NONE") == 0) return 11;

    char* endptr = nullptr;
    long val = strtol(str, &endptr, 10);
    if (endptr != str && val >= 0 && val <= 11) {
        return (uint8_t)val;
    }
    return defaultLevel;
}

Config& config = Config::getInstance();
