#include <unity.h>
#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "Storage.h"

void setUp(void) {
    storage.clear();
}

void tearDown(void) {
    storage.clear();
}

void test_config_from_json_object_complete(void) {
    JsonDocument doc;
    doc["wifiSSID"] = "MyOfficeWifi";
    doc["wifiPassword"] = "SecretPass123";
    doc["sntpServer"] = "time.google.com";
    doc["offsetHours"] = 2;
    doc["checkInterval"] = 30;
    doc["discordWebhookURL"] = "https://discord.com/api/webhooks/123/abc";
    doc["hostname"] = "custom_uptime_host";

    config_t cfg = config.fromJsonObject(doc.as<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("MyOfficeWifi", cfg.wifiSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("SecretPass123", cfg.wifiPassword.c_str());
    TEST_ASSERT_EQUAL_STRING("time.google.com", cfg.sntpServer.c_str());
    TEST_ASSERT_EQUAL_INT8(2, cfg.offsetHours);
    TEST_ASSERT_EQUAL_UINT8(30, cfg.checkInterval);
    TEST_ASSERT_EQUAL_STRING("https://discord.com/api/webhooks/123/abc", cfg.discordWebhookURL.c_str());
    TEST_ASSERT_EQUAL_STRING("custom_uptime_host", cfg.hostname.c_str());
}

void test_config_from_json_object_missing_fields_use_defaults(void) {
    JsonDocument doc;
    // wifiSSID and password empty, other fields omitted
    doc["wifiSSID"] = "TestNet";

    config_t cfg = config.fromJsonObject(doc.as<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("TestNet", cfg.wifiSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("", cfg.wifiPassword.c_str());
    TEST_ASSERT_EQUAL_STRING(DEFAULT_SNTP_SERVER, cfg.sntpServer.c_str());
    TEST_ASSERT_EQUAL_INT8(DEFAULT_TIMEZONE_OFFSET_HOURS, cfg.offsetHours);
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_CHECK_INTERVAL_SECONDS, cfg.checkInterval);
    TEST_ASSERT_EQUAL_STRING("", cfg.discordWebhookURL.c_str());
    TEST_ASSERT_EQUAL_STRING(DEFAULT_HOSTNAME, cfg.hostname.c_str());
}

void test_config_to_json_and_from_json_roundtrip(void) {
    config_t original;
    original.wifiSSID = "HomeNet";
    original.wifiPassword = "Password99";
    original.sntpServer = "0.hu.pool.ntp.org";
    original.offsetHours = 1;
    original.checkInterval = 45;
    original.discordWebhookURL = "https://discord.gg/webhook";
    original.hostname = "sensor_node";

    String jsonStr = config.toJson(original);
    TEST_ASSERT_TRUE(jsonStr.length() > 0);

    config_t restored = config.fromJson(jsonStr);
    TEST_ASSERT_EQUAL_STRING(original.wifiSSID.c_str(), restored.wifiSSID.c_str());
    TEST_ASSERT_EQUAL_STRING(original.wifiPassword.c_str(), restored.wifiPassword.c_str());
    TEST_ASSERT_EQUAL_STRING(original.sntpServer.c_str(), restored.sntpServer.c_str());
    TEST_ASSERT_EQUAL_INT8(original.offsetHours, restored.offsetHours);
    TEST_ASSERT_EQUAL_UINT8(original.checkInterval, restored.checkInterval);
    TEST_ASSERT_EQUAL_STRING(original.discordWebhookURL.c_str(), restored.discordWebhookURL.c_str());
    TEST_ASSERT_EQUAL_STRING(original.hostname.c_str(), restored.hostname.c_str());
}

void test_config_from_json_invalid_syntax_returns_empty(void) {
    String invalidJson = "{ corrupt json string : ";
    config_t cfg = config.fromJson(invalidJson);
    TEST_ASSERT_EQUAL_STRING("", cfg.wifiSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("", cfg.wifiPassword.c_str());
}

void test_config_load_config_file_missing_or_empty(void) {
    // storage.files is empty
    bool success = config.loadConfig();
    TEST_ASSERT_FALSE(success);
}

void test_config_load_config_file_valid(void) {
    String validContent = "{\"wifiSSID\":\"OfficeNet\",\"wifiPassword\":\"SafePass\",\"sntpServer\":\"pool.ntp.org\",\"offsetHours\":1,\"checkInterval\":60,\"discordWebhookURL\":\"\",\"hostname\":\"office_monitor\"}";
    storage.writeFile(CONFIG_FILE_PATH, validContent);

    bool success = config.loadConfig();
    TEST_ASSERT_TRUE(success);

    config_t cfg = config.getConfig();
    TEST_ASSERT_EQUAL_STRING("OfficeNet", cfg.wifiSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("SafePass", cfg.wifiPassword.c_str());
    TEST_ASSERT_EQUAL_STRING("office_monitor", cfg.hostname.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_config_from_json_object_complete);
    RUN_TEST(test_config_from_json_object_missing_fields_use_defaults);
    RUN_TEST(test_config_to_json_and_from_json_roundtrip);
    RUN_TEST(test_config_from_json_invalid_syntax_returns_empty);
    RUN_TEST(test_config_load_config_file_missing_or_empty);
    RUN_TEST(test_config_load_config_file_valid);
    return UNITY_END();
}
