#include "NotificationService.h"
#include <ArduinoJson.h>
#include <Elog.h>
#include "Config.h"

NotificationService& NotificationService::getInstance() {
    static NotificationService instance;
    return instance;
}

void NotificationService::initContext() {
    if (client == nullptr) {
        client = new WiFiClientSecure();
        client->setInsecure();
    }
    if (http == nullptr) {
        http = new HTTPClient();
    }
}

bool NotificationService::send(const String& message, uint32_t color) {
    return send(config.getConfig().discordWebhookURL, message, color);
}

bool NotificationService::send(const String& url, const String& message, uint32_t color) {
    if (url.length() == 0 || message.length() == 0) return false;

    initContext();
    bool success = false;
    if (http->begin(*client, url)) {
        http->setReuse(false);
        http->setTimeout(HTTP_REQUEST_TIMEOUT);
        JsonDocument doc;
        JsonArray embeds = doc["embeds"].to<JsonArray>();
        JsonObject embed = embeds.add<JsonObject>();
        embed["title"] = "ESP32 Service Uptime Monitor";
        embed["description"] = message;
        embed["color"] = color;

        String payload;
        payload.reserve(measureJson(doc) + 1);
        serializeJson(doc, payload);

        http->addHeader("Content-Type", "application/json");
        int16_t httpCode = http->POST(payload);

        if (httpCode >= 200 && httpCode < 300) {
            //Logger.debug(MAINLOG, "Discord notification sent successfully.");
            success = true;
        } else {
            Logger.error(MAINLOG, "Discord notification failed. Code: %d, Error: %s",
                    httpCode, http->errorToString(httpCode).c_str());
        }
        client->stop();
        http->end();
    }
    return success;
}

NotificationService& notificationService = NotificationService::getInstance();
