#include <unity.h>
#include <Arduino.h>
#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include "MonitoringService.h"
#include "MonitoredEntityRepository.h"
#include "Config.h"
#include "Storage.h"
#include "HTTPClient.h"
#include "WiFi.h"

using namespace fakeit;

// Reference mock globals
#include "../mocks/MockGlobals.cpp"

void setUp(void) {
    storage.clear();
    HTTPClient::resetMock();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    auto entities = monitoredEntityRepository.getEntitiesSnapshot();
    for (const auto& e : entities) {
        monitoredEntityRepository.removeEntity(e.getName());
    }
}

void tearDown(void) {
    storage.clear();
    HTTPClient::resetMock();
}

void test_monitoring_service_init(void) {
    // Write 2 entities to storage
    MonitoredEntity e1("srv1", "http://srv1.local", "mdi-server", true);
    MonitoredEntity e2("srv2", "http://srv2.local", "mdi-server", false);
    monitoredEntityRepository.addEntity(e1);
    monitoredEntityRepository.addEntity(e2);

    monitoringService.init();

    String summaryJson = monitoringService.summary();
    JsonDocument doc;
    deserializeJson(doc, summaryJson);
    TEST_ASSERT_EQUAL_UINT(2, doc["total"].as<unsigned int>());
}

void test_monitoring_service_summary_format(void) {
    String summaryJson = monitoringService.summary();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, summaryJson);

    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_TRUE(doc["total"].is<unsigned int>());
    TEST_ASSERT_TRUE(doc["online"].is<uint8_t>());
    TEST_ASSERT_TRUE(doc["offline"].is<uint8_t>());
}

void test_monitoring_service_check_http_success(void) {
    HTTPClient::mockResponseCode = 200;
    MonitoredEntity e("http_node", "http://192.168.1.50/api", "mdi-server", true);

    TEST_ASSERT_TRUE(monitoringService.check(e));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/api", HTTPClient::lastUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAD", HTTPClient::lastMethod.c_str());
}

void test_monitoring_service_check_https_client(void) {
    HTTPClient::mockResponseCode = 200;
    MonitoredEntity e("https_node", "https://secure.example.com", "mdi-web", true);

    TEST_ASSERT_TRUE(monitoringService.check(e));
    TEST_ASSERT_EQUAL_STRING("https://secure.example.com", HTTPClient::lastUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("HEAD", HTTPClient::lastMethod.c_str());
}

void test_monitoring_service_check_http_failure(void) {
    MockWiFiClass::hostByNameCalls = 0;
    HTTPClient::mockResponseCode = -1; // Connection failed
    MonitoredEntity e("dead_node", "http://unreachable.host", "mdi-server", true);

    TEST_ASSERT_FALSE(monitoringService.check(e));
    TEST_ASSERT_EQUAL_INT(1, MockWiFiClass::hostByNameCalls);
}

void test_monitoring_service_check_http_failure_ip_skips_dns(void) {
    MockWiFiClass::hostByNameCalls = 0;
    HTTPClient::mockResponseCode = -1; // Connection failed
    MonitoredEntity e("dead_ip", "http://192.168.1.99:8080/health", "mdi-server", true);

    TEST_ASSERT_FALSE(monitoringService.check(e));
    TEST_ASSERT_EQUAL_INT(0, MockWiFiClass::hostByNameCalls);
}

void test_monitoring_service_checkall_transitions_to_offline_after_3_failures(void) {
    // Configure webhook in config
    storage.writeFile(CONFIG_FILE_PATH, "{\"discordWebhookURL\":\"https://discord.com/api/webhooks/test\"}");
    config.loadConfig();

    MonitoredEntity e("crit_srv", "http://critical.local", "mdi-server", true);
    e.setOnlineStatus(true);
    monitoredEntityRepository.addEntity(e);

    // Run 1: Failure 1
    HTTPClient::mockResponseCode = -1;
    monitoringService.checkAll();
    MonitoredEntity check1;
    monitoredEntityRepository.getEntity("crit_srv", check1);
    TEST_ASSERT_EQUAL_UINT8(1, check1.getFailCount());
    TEST_ASSERT_TRUE(check1.isOnline());
    TEST_ASSERT_EQUAL_STRING("HEAD", HTTPClient::lastMethod.c_str());

    // Run 2: Failure 2
    HTTPClient::mockResponseCode = -1;
    monitoringService.checkAll();
    MonitoredEntity check2;
    monitoredEntityRepository.getEntity("crit_srv", check2);
    TEST_ASSERT_EQUAL_UINT8(2, check2.getFailCount());
    TEST_ASSERT_TRUE(check2.isOnline());

    // Run 3: Failure 3 (Threshold reached -> transitions to OFFLINE, sends Discord notification)
    HTTPClient::mockResponseCode = -1;
    monitoringService.checkAll();
    MonitoredEntity check3;
    monitoredEntityRepository.getEntity("crit_srv", check3);
    TEST_ASSERT_EQUAL_UINT8(3, check3.getFailCount());
    TEST_ASSERT_FALSE(check3.isOnline());

    // Verify Discord notification was sent via POST
    TEST_ASSERT_EQUAL_STRING("https://discord.com/api/webhooks/test", HTTPClient::lastUrl.c_str());
    TEST_ASSERT_EQUAL_STRING("POST", HTTPClient::lastMethod.c_str());
    TEST_ASSERT_TRUE(HTTPClient::lastPayload.indexOf("crit_srv") > 0);
    TEST_ASSERT_TRUE(HTTPClient::lastPayload.indexOf("OFFLINE") > 0);

    // Run 4: Failure 4 -> already OFFLINE, no new notification sent
    HTTPClient::resetMock();
    HTTPClient::mockResponseCode = -1;
    monitoringService.checkAll();
    TEST_ASSERT_EQUAL_STRING("HEAD", HTTPClient::lastMethod.c_str());
}

void test_monitoring_service_checkall_recovery_on_success(void) {
    storage.writeFile(CONFIG_FILE_PATH, "{\"discordWebhookURL\":\"https://discord.com/api/webhooks/test\"}");
    config.loadConfig();

    MonitoredEntity e("rec_srv", "http://recovered.local", "mdi-server", true);
    e.setOnlineStatus(true);
    e.setOnlineStatus(false); // set to OFFLINE with valid lastChanged
    monitoredEntityRepository.addEntity(e);

    // Perform checkAll with success response
    HTTPClient::mockResponseCode = 200;
    monitoringService.checkAll();

    MonitoredEntity checkRec;
    monitoredEntityRepository.getEntity("rec_srv", checkRec);
    TEST_ASSERT_TRUE(checkRec.isOnline());
    TEST_ASSERT_EQUAL_UINT8(0, checkRec.getFailCount());

    // Verify recovery notification sent
    TEST_ASSERT_EQUAL_STRING("POST", HTTPClient::lastMethod.c_str());
    TEST_ASSERT_TRUE(HTTPClient::lastPayload.indexOf("rec_srv") > 0);
    TEST_ASSERT_TRUE(HTTPClient::lastPayload.indexOf("ONLINE") > 0);
}

void test_monitoring_service_checkall_skips_disabled(void) {
    MonitoredEntity e("dis_node", "http://disabled.local", "mdi-server", true);
    e.setDisabled(true);
    monitoredEntityRepository.addEntity(e);

    HTTPClient::resetMock();
    monitoringService.checkAll();

    // No request should have been made
    TEST_ASSERT_EQUAL_STRING("", HTTPClient::lastUrl.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_monitoring_service_init);
    RUN_TEST(test_monitoring_service_summary_format);
    RUN_TEST(test_monitoring_service_check_http_success);
    RUN_TEST(test_monitoring_service_check_https_client);
    RUN_TEST(test_monitoring_service_check_http_failure);
    RUN_TEST(test_monitoring_service_check_http_failure_ip_skips_dns);
    RUN_TEST(test_monitoring_service_checkall_transitions_to_offline_after_3_failures);
    RUN_TEST(test_monitoring_service_checkall_recovery_on_success);
    RUN_TEST(test_monitoring_service_checkall_skips_disabled);
    return UNITY_END();
}
