#include "NotificationService.h"
#include <ArduinoJson.h>
#include <Elog.h>
#include "Config.h"

#ifndef UNIT_TEST
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
#endif

NotificationService& NotificationService::getInstance() {
    static NotificationService instance;
    return instance;
}

bool NotificationService::send(const String& message, uint32_t color) {
    return send(config.getConfig().discordWebhookURL, message, color);
}

bool NotificationService::send(const String& url, const String& message, uint32_t color) {
    if (url.length() == 0 || message.length() == 0) return false;

    WiFiClientSecure client;
#ifndef UNIT_TEST
    client.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
#else
    client.setInsecure();
#endif
    HTTPClient http;

    bool success = false;
    if (http.begin(client, url)) {
        http.setReuse(false);
        http.setTimeout(HTTP_REQUEST_TIMEOUT);
        JsonDocument doc;
        JsonArray embeds = doc["embeds"].to<JsonArray>();
        JsonObject embed = embeds.add<JsonObject>();
        embed["title"] = "ESP32 Service Uptime Monitor";
        embed["description"] = message;
        embed["color"] = color;

        String payload;
        payload.reserve(measureJson(doc) + 1);
        serializeJson(doc, payload);

        http.addHeader("Content-Type", "application/json");
        int16_t httpCode = http.POST(payload);

        if (httpCode >= 200 && httpCode < 300) {
            success = true;
        } else {
            Logger.error(MAINLOG, "Discord notification failed. Code: %d, Error: %s",
                    httpCode, http.errorToString(httpCode).c_str());
        }
        client.stop();
        http.end();
    }
    return success;
}

NotificationService& notificationService = NotificationService::getInstance();
