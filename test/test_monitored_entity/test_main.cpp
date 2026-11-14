#include <unity.h>
#include <ArduinoFake.h>
#include <ArduinoJson.h>
#include "MonitoredEntity.h"
#include "../mocks/MockGlobals.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_default_constructor(void) {
    MonitoredEntity entity;
    TEST_ASSERT_EQUAL_STRING("", entity.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("", entity.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("", entity.getIcon().c_str());
    TEST_ASSERT_FALSE(entity.isOnline());
    TEST_ASSERT_TRUE(entity.getNotification());
    TEST_ASSERT_FALSE(entity.isDisabled());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)entity.getLastChanged());
    TEST_ASSERT_EQUAL_UINT8(0, entity.getFailCount());
}

void test_parameterized_constructor(void) {
    MonitoredEntity entity("Server A", "https://example.com/health", "mdi-server", false);
    TEST_ASSERT_EQUAL_STRING("Server A", entity.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("https://example.com/health", entity.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("mdi-server", entity.getIcon().c_str());
    TEST_ASSERT_FALSE(entity.isOnline());
    TEST_ASSERT_FALSE(entity.getNotification());
    TEST_ASSERT_FALSE(entity.isDisabled());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)entity.getLastChanged());
    TEST_ASSERT_EQUAL_UINT8(0, entity.getFailCount());
}

void test_setters_and_getters(void) {
    MonitoredEntity entity;
    String name = "Router";
    String addr = "http://192.168.1.1";
    String icon = "mdi-router-wireless";

    entity.setName(name);
    entity.setAddress(addr);
    entity.setIcon(icon);
    entity.setNotification(false);
    entity.setDisabled(true);
    entity.setLastChanged(1700000000);

    TEST_ASSERT_EQUAL_STRING("Router", entity.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.1", entity.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("mdi-router-wireless", entity.getIcon().c_str());
    TEST_ASSERT_FALSE(entity.getNotification());
    TEST_ASSERT_TRUE(entity.isDisabled());
    TEST_ASSERT_EQUAL_UINT32(1700000000, (uint32_t)entity.getLastChanged());
}

void test_fail_count_operations(void) {
    MonitoredEntity entity;
    TEST_ASSERT_EQUAL_UINT8(0, entity.getFailCount());

    entity.incrementFailCount();
    TEST_ASSERT_EQUAL_UINT8(1, entity.getFailCount());

    entity.incrementFailCount();
    entity.incrementFailCount();
    TEST_ASSERT_EQUAL_UINT8(3, entity.getFailCount());

    entity.resetFailCount();
    TEST_ASSERT_EQUAL_UINT8(0, entity.getFailCount());
}

void test_online_status_transition_and_timestamp(void) {
    MonitoredEntity entity("Web", "http://web.local", "mdi-web", true);
    TEST_ASSERT_FALSE(entity.isOnline());
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)entity.getLastChanged());

    entity.incrementFailCount();
    entity.incrementFailCount();
    TEST_ASSERT_EQUAL_UINT8(2, entity.getFailCount());

    // Transition offline -> online: status becomes true, failCount resets to 0, lastChanged set
    entity.setOnlineStatus(true);
    TEST_ASSERT_TRUE(entity.isOnline());
    TEST_ASSERT_EQUAL_UINT8(0, entity.getFailCount());
    TEST_ASSERT_TRUE(entity.getLastChanged() > 0);

    time_t recordedTime = entity.getLastChanged();

    // Transition online -> online: same state, lastChanged does NOT change
    entity.setOnlineStatus(true);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)recordedTime, (uint32_t)entity.getLastChanged());

    // Transition online -> offline: status becomes false
    entity.setOnlineStatus(false);
    TEST_ASSERT_FALSE(entity.isOnline());
}

void test_to_json_serialization(void) {
    MonitoredEntity entity("Gateway", "https://gateway.internal", "mdi-lan", true);
    entity.setDisabled(false);
    entity.setLastChanged(1705000000);

    String jsonStr = entity.toJson();
    TEST_ASSERT_TRUE(jsonStr.length() > 0);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    TEST_ASSERT_FALSE(err);

    TEST_ASSERT_EQUAL_STRING("Gateway", doc["name"]);
    TEST_ASSERT_EQUAL_STRING("https://gateway.internal", doc["address"]);
    TEST_ASSERT_EQUAL_STRING("mdi-lan", doc["icon"]);
    TEST_ASSERT_FALSE(doc["isOnline"].as<bool>());
    TEST_ASSERT_TRUE(doc["notification"].as<bool>());
    TEST_ASSERT_FALSE(doc["disabled"].as<bool>());
    TEST_ASSERT_EQUAL_UINT32(1705000000, doc["lastChanged"].as<uint32_t>());
}

void test_from_json_object_valid(void) {
    JsonDocument doc;
    doc["name"] = "Database";
    doc["address"] = "https://db.lan:8080";
    doc["icon"] = "mdi-database";
    doc["isOnline"] = true;
    doc["notification"] = false;
    doc["disabled"] = true;
    doc["lastChanged"] = 1710000000;

    MonitoredEntity entity;
    bool success = entity.fromJsonObject(doc.as<JsonObject>());
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_STRING("Database", entity.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("https://db.lan:8080", entity.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("mdi-database", entity.getIcon().c_str());
    TEST_ASSERT_TRUE(entity.isOnline());
    TEST_ASSERT_FALSE(entity.getNotification());
    TEST_ASSERT_TRUE(entity.isDisabled());
    TEST_ASSERT_EQUAL_UINT32(1710000000, (uint32_t)entity.getLastChanged());
}

void test_from_json_object_missing_required_fields(void) {
    MonitoredEntity entity;

    // Missing address
    JsonDocument docNoAddr;
    docNoAddr["name"] = "NoAddress";
    TEST_ASSERT_FALSE(entity.fromJsonObject(docNoAddr.as<JsonObject>()));

    // Missing name
    JsonDocument docNoName;
    docNoName["address"] = "http://10.0.0.1";
    TEST_ASSERT_FALSE(entity.fromJsonObject(docNoName.as<JsonObject>()));

    // Empty object
    JsonDocument docEmpty;
    TEST_ASSERT_FALSE(entity.fromJsonObject(docEmpty.as<JsonObject>()));
}

void test_from_json_string_valid_and_invalid(void) {
    MonitoredEntity entity;

    // Valid JSON string
    String validJson = "{\"name\":\"API\",\"address\":\"https://api.io\",\"icon\":\"mdi-cloud\",\"isOnline\":true,\"notification\":true,\"disabled\":false,\"lastChanged\":1709999999}";
    TEST_ASSERT_TRUE(entity.fromJson(validJson));
    TEST_ASSERT_EQUAL_STRING("API", entity.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("https://api.io", entity.getAddress().c_str());
    TEST_ASSERT_TRUE(entity.isOnline());

    // Invalid JSON syntax
    String corruptJson = "{corrupted json:";
    TEST_ASSERT_FALSE(entity.fromJson(corruptJson));
}

void test_copy_assignment_operator(void) {
    MonitoredEntity source("Original", "http://orig.local", "mdi-server", true);
    source.incrementFailCount();
    source.incrementFailCount();
    source.setOnlineStatus(true);
    source.setDisabled(true);

    MonitoredEntity copy;
    copy = source;

    TEST_ASSERT_EQUAL_STRING("Original", copy.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("http://orig.local", copy.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("mdi-server", copy.getIcon().c_str());
    TEST_ASSERT_TRUE(copy.isOnline());
    TEST_ASSERT_TRUE(copy.getNotification());
    TEST_ASSERT_TRUE(copy.isDisabled());
    TEST_ASSERT_EQUAL_UINT8(0, copy.getFailCount()); // failCount was reset when setOnlineStatus(true) was called
    TEST_ASSERT_EQUAL_UINT32((uint32_t)source.getLastChanged(), (uint32_t)copy.getLastChanged());

    // Verify mutating copy does not mutate source
    String newName = "Mutated";
    copy.setName(newName);
    TEST_ASSERT_EQUAL_STRING("Original", source.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("Mutated", copy.getName().c_str());
}

void test_is_valid_address(void) {
    TEST_ASSERT_TRUE(MonitoredEntity::isValidAddress("http://example.com"));
    TEST_ASSERT_TRUE(MonitoredEntity::isValidAddress("https://192.168.1.1:8443/status"));
    TEST_ASSERT_TRUE(MonitoredEntity::isValidAddress("http://server.local"));

    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("ftp://example.com"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://localhost"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://localhost:8080/admin"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://127.0.0.1"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://127.0.0.5:9000"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://169.254.169.254/latest/meta-data"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://0.0.0.0"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://224.0.0.1"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://255.255.255.255"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://[::1]"));

    // R-03 bypass tests: userinfo rejection
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://x@169.254.169.254/"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://admin:pass@example.com/"));

    // Alternative IPv4 notation (octal, hex, dword, short forms)
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://127.1/"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://2130706433/"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://0x7f.1/"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://0177.0.0.1/"));

    // IPv4-mapped IPv6 literals
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://[::ffff:127.0.0.1]/"));
    TEST_ASSERT_FALSE(MonitoredEntity::isValidAddress("http://[::ffff:169.254.169.254]/"));
}

void test_is_valid_for_user_input(void) {
    MonitoredEntity valid("valid-node", "http://192.168.1.50/health", "mdi-server", true);
    TEST_ASSERT_TRUE(valid.isValidForUserInput());

    // Valid Hungarian and punctuation characters in name
    MonitoredEntity accented("Árvíztűrő Fúrógép_01-test.srv", "http://192.168.1.50/health", "mdi-server", true);
    TEST_ASSERT_TRUE(accented.isValidForUserInput());

    // Name length > 63
    String longName = "a";
    while (longName.length() <= 63) longName += "a";
    MonitoredEntity oversizeName(longName, "http://192.168.1.50/health", "mdi-server", true);
    TEST_ASSERT_FALSE(oversizeName.isValidForUserInput());

    // Leading / trailing space in name
    MonitoredEntity leadingSpace(" test", "http://192.168.1.50/health", "mdi-server", true);
    TEST_ASSERT_FALSE(leadingSpace.isValidForUserInput());
    MonitoredEntity trailingSpace("test ", "http://192.168.1.50/health", "mdi-server", true);
    TEST_ASSERT_FALSE(trailingSpace.isValidForUserInput());

    // Address length > 127
    String longAddr = "http://example.com/";
    while (longAddr.length() <= 127) longAddr += "x";
    MonitoredEntity oversizeAddr("valid-name", longAddr, "mdi-server", true);
    TEST_ASSERT_FALSE(oversizeAddr.isValidForUserInput());

    // Icon length > 127
    String longIcon = "mdi-";
    while (longIcon.length() <= 127) longIcon += "i";
    MonitoredEntity oversizeIcon("valid-name", "http://192.168.1.50/health", longIcon, true);
    TEST_ASSERT_FALSE(oversizeIcon.isValidForUserInput());

    // Prohibited characters in name
    MonitoredEntity badChars("bad/name", "http://192.168.1.50/health", "mdi-server", true);
    TEST_ASSERT_FALSE(badChars.isValidForUserInput());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_default_constructor);
    RUN_TEST(test_parameterized_constructor);
    RUN_TEST(test_setters_and_getters);
    RUN_TEST(test_fail_count_operations);
    RUN_TEST(test_online_status_transition_and_timestamp);
    RUN_TEST(test_to_json_serialization);
    RUN_TEST(test_from_json_object_valid);
    RUN_TEST(test_from_json_object_missing_required_fields);
    RUN_TEST(test_from_json_string_valid_and_invalid);
    RUN_TEST(test_copy_assignment_operator);
    RUN_TEST(test_is_valid_address);
    RUN_TEST(test_is_valid_for_user_input);
    return UNITY_END();
}
