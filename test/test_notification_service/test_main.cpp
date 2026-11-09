#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "NotificationService.h"
#include "Config.h"
#include "Storage.h"
#include "HTTPClient.h"
#include "../mocks/MockGlobals.cpp"

void setUp(void) {
    storage.clear();
    HTTPClient::resetMock();
}

void tearDown(void) {
    storage.clear();
    HTTPClient::resetMock();
}

void test_notification_empty_url_returns_false(void) {
    TEST_ASSERT_FALSE(notificationService.send("", "Server down", DISCORD_COLOR_RED));
    TEST_ASSERT_EQUAL_STRING("", HTTPClient::lastUrl.c_str());
}

void test_notification_empty_message_returns_false(void) {
    TEST_ASSERT_FALSE(notificationService.send("https://discord.com/api/webhooks/test", "", DISCORD_COLOR_RED));
    TEST_ASSERT_EQUAL_STRING("", HTTPClient::lastUrl.c_str());
}

void test_notification_send_success(void) {
    HTTPClient::mockResponseCode = 204; // Discord success code
    const String webhook = "https://discord.com/api/webhooks/123/token";
    const String message = "Host alpha is OFFLINE";
    const uint32_t color = DISCORD_COLOR_RED;

    TEST_ASSERT_TRUE(notificationService.send(webhook, message, color));

    TEST_ASSERT_EQUAL_STRING(webhook.c_str(), HTTPClient::lastUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("POST", HTTPClient::lastMethod.c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", HTTPClient::lastHeaders["Content-Type"].c_str());

    // Validate JSON payload structure
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, HTTPClient::lastPayload);
    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_TRUE(doc["embeds"].is<JsonArray>());

    JsonArray embeds = doc["embeds"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT(1, embeds.size());
    TEST_ASSERT_EQUAL_STRING("ESP32 Service Uptime Monitor", embeds[0]["title"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("Host alpha is OFFLINE", embeds[0]["description"].as<const char*>());
    TEST_ASSERT_EQUAL_UINT32(DISCORD_COLOR_RED, embeds[0]["color"].as<uint32_t>());
}

void test_notification_send_http_failure(void) {
    HTTPClient::mockResponseCode = 400; // Bad request
    const String webhook = "https://discord.com/api/webhooks/123/token";

    TEST_ASSERT_FALSE(notificationService.send(webhook, "Test message", DISCORD_COLOR_GREEN));
}

void test_notification_send_default_webhook_from_config(void) {
    HTTPClient::mockResponseCode = 200;

    // Set discordWebhookURL in config via storage + loadConfig
    storage.writeFile(CONFIG_FILE_PATH, "{\"discordWebhookURL\":\"https://discord.com/api/webhooks/default/url\"}");
    config.loadConfig();

    TEST_ASSERT_TRUE(notificationService.send("Default webhook notification", DISCORD_COLOR_BLUE));
    TEST_ASSERT_EQUAL_STRING("https://discord.com/api/webhooks/default/url", HTTPClient::lastUrl.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_notification_empty_url_returns_false);
    RUN_TEST(test_notification_empty_message_returns_false);
    RUN_TEST(test_notification_send_success);
    RUN_TEST(test_notification_send_http_failure);
    RUN_TEST(test_notification_send_default_webhook_from_config);
    return UNITY_END();
}
