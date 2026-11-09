#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "MonitoredEntityRepository.h"
#include "Storage.h"
#include "../mocks/MockGlobals.cpp"

void setUp(void) {
    storage.clear();
    // Clear repository entities between tests
    auto entities = monitoredEntityRepository.getEntitiesSnapshot();
    for (const auto& e : entities) {
        monitoredEntityRepository.removeEntity(e.getName());
    }
}

void tearDown(void) {
    storage.clear();
}

// 1. Empty repository initially has 0 size and 0 counts
void test_repository_empty_initial_state(void) {
    TEST_ASSERT_EQUAL_UINT(0, monitoredEntityRepository.size());
    uint8_t online = 99, offline = 99;
    unsigned int total = 99;
    monitoredEntityRepository.getCounts(online, offline, total);
    TEST_ASSERT_EQUAL_UINT8(0, online);
    TEST_ASSERT_EQUAL_UINT8(0, offline);
    TEST_ASSERT_EQUAL_UINT(0, total);
}

// 2. Add entity and retrieve it
void test_repository_add_entity_and_get(void) {
    MonitoredEntity entity("server1", "http://192.168.1.100", "mdi-server", true);
    monitoredEntityRepository.addEntity(entity);

    TEST_ASSERT_EQUAL_UINT(1, monitoredEntityRepository.size());
    TEST_ASSERT_TRUE(monitoredEntityRepository.hasEntity("server1"));
    TEST_ASSERT_FALSE(monitoredEntityRepository.hasEntity("server2"));

    MonitoredEntity outEntity;
    TEST_ASSERT_TRUE(monitoredEntityRepository.getEntity("server1", outEntity));
    TEST_ASSERT_EQUAL_STRING("server1", outEntity.getName().c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.100", outEntity.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("mdi-server", outEntity.getIcon().c_str());
    TEST_ASSERT_TRUE(outEntity.getNotification());
    TEST_ASSERT_FALSE(outEntity.isDisabled());
}

// 3. Update existing entity
void test_repository_update_entity(void) {
    MonitoredEntity entity("webserver", "http://old.local", "mdi-web", false);
    monitoredEntityRepository.addEntity(entity);

    MonitoredEntity updated("webserver", "https://new.local", "mdi-shield", true);
    updated.setDisabled(true);
    TEST_ASSERT_TRUE(monitoredEntityRepository.updateEntity("webserver", updated));

    MonitoredEntity outEntity;
    TEST_ASSERT_TRUE(monitoredEntityRepository.getEntity("webserver", outEntity));
    TEST_ASSERT_EQUAL_STRING("https://new.local", outEntity.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("mdi-shield", outEntity.getIcon().c_str());
    TEST_ASSERT_TRUE(outEntity.getNotification());
    TEST_ASSERT_TRUE(outEntity.isDisabled());
}

// 4. Update non-existent entity returns false
void test_repository_update_nonexistent_returns_false(void) {
    MonitoredEntity updated("ghost", "http://ghost.local", "mdi-server", false);
    TEST_ASSERT_FALSE(monitoredEntityRepository.updateEntity("ghost", updated));
}

// 5. Remove entity
void test_repository_remove_entity(void) {
    MonitoredEntity e1("node1", "http://n1.local", "mdi-server", true);
    MonitoredEntity e2("node2", "http://n2.local", "mdi-server", true);
    monitoredEntityRepository.addEntity(e1);
    monitoredEntityRepository.addEntity(e2);

    TEST_ASSERT_EQUAL_UINT(2, monitoredEntityRepository.size());
    TEST_ASSERT_TRUE(monitoredEntityRepository.removeEntity("node1"));
    TEST_ASSERT_EQUAL_UINT(1, monitoredEntityRepository.size());
    TEST_ASSERT_FALSE(monitoredEntityRepository.hasEntity("node1"));
    TEST_ASSERT_TRUE(monitoredEntityRepository.hasEntity("node2"));
}

// 6. Remove non-existent entity returns false
void test_repository_remove_nonexistent_returns_false(void) {
    TEST_ASSERT_FALSE(monitoredEntityRepository.removeEntity("unknown_node"));
}

// 7. Snapshot isolation
void test_repository_get_entities_snapshot(void) {
    MonitoredEntity e1("node1", "http://n1.local", "mdi-server", true);
    monitoredEntityRepository.addEntity(e1);

    auto snapshot = monitoredEntityRepository.getEntitiesSnapshot();
    TEST_ASSERT_EQUAL_UINT(1, snapshot.size());

    // Modifying snapshot does not modify repository
    String modAddr = "http://modified.local";
    snapshot[0].setAddress(modAddr);
    MonitoredEntity checkEntity;
    monitoredEntityRepository.getEntity("node1", checkEntity);
    TEST_ASSERT_EQUAL_STRING("http://n1.local", checkEntity.getAddress().c_str());
}

// 8. Entity status counts calculation
void test_repository_get_counts(void) {
    MonitoredEntity e1("online_node", "http://online.local", "mdi-server", true);
    e1.setOnlineStatus(true); // online = true, lastChanged = now

    MonitoredEntity e2("offline_node", "http://offline.local", "mdi-server", true);
    e2.setOnlineStatus(true);
    e2.setOnlineStatus(false); // online = false, lastChanged = now

    MonitoredEntity e3("pending_node", "http://pending.local", "mdi-server", true);
    // lastChanged = 0 (untested yet)

    MonitoredEntity e4("disabled_node", "http://disabled.local", "mdi-server", true);
    e4.setDisabled(true);
    e4.setOnlineStatus(true); // disabled, should be excluded from online/offline count

    monitoredEntityRepository.addEntity(e1);
    monitoredEntityRepository.addEntity(e2);
    monitoredEntityRepository.addEntity(e3);
    monitoredEntityRepository.addEntity(e4);

    uint8_t online = 0, offline = 0;
    unsigned int total = 0;
    monitoredEntityRepository.getCounts(online, offline, total);

    TEST_ASSERT_EQUAL_UINT(4, total);
    TEST_ASSERT_EQUAL_UINT8(1, online);
    TEST_ASSERT_EQUAL_UINT8(1, offline);
}

// 9. Failure progression (3 failures transition to OFFLINE)
void test_repository_update_check_result_failure_progression(void) {
    MonitoredEntity e("test_srv", "http://test.local", "mdi-server", true);
    e.setOnlineStatus(true);
    monitoredEntityRepository.addEntity(e);

    bool statusChanged = false, newStatus = false, notify = false;

    // Failure 1: failCount becomes 1, status does not change yet (returns false)
    TEST_ASSERT_FALSE(monitoredEntityRepository.updateCheckResult("test_srv", false, statusChanged, newStatus, notify));
    TEST_ASSERT_FALSE(statusChanged);

    // Failure 2: failCount becomes 2, status does not change yet (returns false)
    TEST_ASSERT_FALSE(monitoredEntityRepository.updateCheckResult("test_srv", false, statusChanged, newStatus, notify));
    TEST_ASSERT_FALSE(statusChanged);

    // Failure 3: failCount becomes 3 (>= HTTP_REQUEST_RETRIES=3) -> triggers status change to OFFLINE (returns true)
    TEST_ASSERT_TRUE(monitoredEntityRepository.updateCheckResult("test_srv", false, statusChanged, newStatus, notify));
    TEST_ASSERT_TRUE(statusChanged);
    TEST_ASSERT_FALSE(newStatus);
    TEST_ASSERT_TRUE(notify);

    // Failure 4: already OFFLINE, no duplicate change (returns false)
    TEST_ASSERT_FALSE(monitoredEntityRepository.updateCheckResult("test_srv", false, statusChanged, newStatus, notify));
    TEST_ASSERT_FALSE(statusChanged);
}

// 10. Success recovery (1 success immediately restores ONLINE)
void test_repository_update_check_result_success_recovery(void) {
    MonitoredEntity e("rec_srv", "http://rec.local", "mdi-server", true);
    e.setOnlineStatus(false);
    monitoredEntityRepository.addEntity(e);

    bool statusChanged = false, newStatus = false, notify = false;

    // Single success restores online
    TEST_ASSERT_TRUE(monitoredEntityRepository.updateCheckResult("rec_srv", true, statusChanged, newStatus, notify));
    TEST_ASSERT_TRUE(statusChanged);
    TEST_ASSERT_TRUE(newStatus);
    TEST_ASSERT_TRUE(notify);

    MonitoredEntity checkEntity;
    monitoredEntityRepository.getEntity("rec_srv", checkEntity);
    TEST_ASSERT_TRUE(checkEntity.isOnline());
    TEST_ASSERT_EQUAL_UINT8(0, checkEntity.getFailCount());
}

// 11. Disabled entities ignored by updateCheckResult
void test_repository_update_check_result_disabled_ignored(void) {
    MonitoredEntity e("dis_srv", "http://dis.local", "mdi-server", true);
    e.setDisabled(true);
    monitoredEntityRepository.addEntity(e);

    bool statusChanged = false, newStatus = false, notify = false;
    TEST_ASSERT_FALSE(monitoredEntityRepository.updateCheckResult("dis_srv", false, statusChanged, newStatus, notify));
}

// 12. Persistence roundtrip (save and load from Storage)
void test_repository_load_and_save_roundtrip(void) {
    MonitoredEntity e1("alpha", "http://alpha.local", "mdi-server", true);
    MonitoredEntity e2("beta", "https://beta.local", "mdi-database", false);
    e2.setDisabled(true);
    monitoredEntityRepository.addEntity(e1);
    monitoredEntityRepository.addEntity(e2);

    // Save was automatically triggered by addEntity, verify file exists in storage
    TEST_ASSERT_TRUE(storage.fileExists(ENTITIES_FILE_PATH));
    String savedJson = storage.readFile(ENTITIES_FILE_PATH);
    TEST_ASSERT_TRUE(savedJson.length() > 0);

    // Clear in-memory repository (which would overwrite storage file)
    monitoredEntityRepository.removeEntity("alpha");
    monitoredEntityRepository.removeEntity("beta");
    TEST_ASSERT_EQUAL_UINT(0, monitoredEntityRepository.size());

    // Restore saved JSON in storage to simulate reloading on reboot
    storage.writeFile(ENTITIES_FILE_PATH, savedJson);

    // Re-load from storage file
    TEST_ASSERT_TRUE(monitoredEntityRepository.load());
    TEST_ASSERT_EQUAL_UINT(2, monitoredEntityRepository.size());

    MonitoredEntity loadedAlpha, loadedBeta;
    TEST_ASSERT_TRUE(monitoredEntityRepository.getEntity("alpha", loadedAlpha));
    TEST_ASSERT_TRUE(monitoredEntityRepository.getEntity("beta", loadedBeta));

    TEST_ASSERT_EQUAL_STRING("http://alpha.local", loadedAlpha.getAddress().c_str());
    TEST_ASSERT_EQUAL_STRING("https://beta.local", loadedBeta.getAddress().c_str());
    TEST_ASSERT_TRUE(loadedAlpha.getNotification());
    TEST_ASSERT_FALSE(loadedBeta.getNotification());
    TEST_ASSERT_FALSE(loadedAlpha.isDisabled());
    TEST_ASSERT_TRUE(loadedBeta.isDisabled());
}

// 13. Load with missing file starts with empty list and returns true
void test_repository_load_missing_file_starts_empty(void) {
    storage.deleteFile(ENTITIES_FILE_PATH);
    TEST_ASSERT_TRUE(monitoredEntityRepository.load());
    TEST_ASSERT_EQUAL_UINT(0, monitoredEntityRepository.size());
}

// 14. Load with corrupted JSON returns false
void test_repository_load_corrupted_json_returns_false(void) {
    storage.writeFile(ENTITIES_FILE_PATH, "{ this is not valid json ]");
    TEST_ASSERT_FALSE(monitoredEntityRepository.load());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_repository_empty_initial_state);
    RUN_TEST(test_repository_add_entity_and_get);
    RUN_TEST(test_repository_update_entity);
    RUN_TEST(test_repository_update_nonexistent_returns_false);
    RUN_TEST(test_repository_remove_entity);
    RUN_TEST(test_repository_remove_nonexistent_returns_false);
    RUN_TEST(test_repository_get_entities_snapshot);
    RUN_TEST(test_repository_get_counts);
    RUN_TEST(test_repository_update_check_result_failure_progression);
    RUN_TEST(test_repository_update_check_result_success_recovery);
    RUN_TEST(test_repository_update_check_result_disabled_ignored);
    RUN_TEST(test_repository_load_and_save_roundtrip);
    RUN_TEST(test_repository_load_missing_file_starts_empty);
    RUN_TEST(test_repository_load_corrupted_json_returns_false);
    return UNITY_END();
}
