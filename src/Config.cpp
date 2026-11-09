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
    return config;
}

Config& config = Config::getInstance();
