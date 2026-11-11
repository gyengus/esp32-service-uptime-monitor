#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoFake.h>
#include "Api.h"
#include "Config.h"
#include "Storage.h"
#include "MonitoredEntityRepository.h"
#include "MonitoringService.h"
#include "NotificationService.h"
#include "NetworkContext.h"
#include "LedService.h"
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "Update.h"

using namespace fakeit;

#include "../mocks/MockGlobals.cpp"

static AsyncWebServer* testServer = nullptr;
static Api* testApi = nullptr;

void setUp(void) {
    storage.clear();
    MockWiFiClass::resetMock();
    Update.resetMock();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(123456);

    isConfigFunctionsEnabled = true;
    isAPMode = false;
    shouldRestart = false;
    isOtaUpdating = false;

    // Reset repo
    auto entities = monitoredEntityRepository.getEntitiesSnapshot();
    for (auto& e : entities) {
        monitoredEntityRepository.removeEntity(e.getName());
    }

    if (testApi) delete testApi;
    if (testServer) delete testServer;

    testServer = new AsyncWebServer(80);
    testApi = new Api(*testServer);
    testApi->registerEndpoints();
}

void tearDown(void) {
    if (testApi) {
        delete testApi;
        testApi = nullptr;
    }
    if (testServer) {
        delete testServer;
        testServer = nullptr;
    }
    ArduinoFakeReset();
    storage.clear();
}

void test_api_health(void) {
    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/health", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
    TEST_ASSERT_TRUE(req.lastBody.indexOf("\"status\":\"UP\"") >= 0);
}

void test_api_sysinfo(void) {
    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/sysinfo", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, req.lastBody);
    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_TRUE(doc["temperature"].is<float>());
    TEST_ASSERT_TRUE(doc["freeHeap"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["maxFreeBlock"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["heapSize"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["minFreeHeap"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["maxAllocHeap"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["uptime"].is<unsigned long>());
    TEST_ASSERT_TRUE(doc["version"].is<const char*>());
    TEST_ASSERT_TRUE(doc["configEnabled"].is<bool>());
    TEST_ASSERT_TRUE(doc["configEnabled"].as<bool>());
}

void test_api_sysinfo_details(void) {
    storage.writeFile(String(WEB_UI_PATH) + "/.version", "1.0.0-abc123\n");

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/sysinfo/details", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, req.lastBody);
    TEST_ASSERT_FALSE(err);

    TEST_ASSERT_TRUE(doc["device"]["chipModel"].is<const char*>());
    TEST_ASSERT_TRUE(doc["device"]["resetReason"].is<const char*>());
    TEST_ASSERT_TRUE(doc["memory"]["maxFreeBlock"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["memory"]["flashSize"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["storage"]["backend"].is<const char*>());
    TEST_ASSERT_TRUE(doc["storage"]["available"].as<bool>());
    TEST_ASSERT_TRUE(doc["network"]["ip"].is<const char*>());
    TEST_ASSERT_TRUE(doc["network"]["mac"].is<const char*>());
    TEST_ASSERT_TRUE(doc["time"]["uptime"].is<unsigned long>());
    TEST_ASSERT_TRUE(doc["monitoring"]["checkInterval"].is<uint8_t>());
    TEST_ASSERT_EQUAL_STRING("1.0.0-abc123", doc["firmware"]["uiVersion"].as<const char*>());
    TEST_ASSERT_TRUE(doc["firmware"]["otaPartition"].is<const char*>());
}

void test_api_config_get(void) {
    // 1. Forbidden when config functions disabled and not AP mode
    isConfigFunctionsEnabled = false;
    isAPMode = false;
    AsyncWebServerRequest req1;
    testServer->dispatch("/api/config", HTTP_GET, &req1);
    TEST_ASSERT_EQUAL_INT(403, req1.lastCode);

    // 2. Allowed when config functions enabled
    isConfigFunctionsEnabled = true;
    AsyncWebServerRequest req2;
    testServer->dispatch("/api/config", HTTP_GET, &req2);
    TEST_ASSERT_EQUAL_INT(200, req2.lastCode);

    JsonDocument doc;
    deserializeJson(doc, req2.lastBody);
    TEST_ASSERT_TRUE(doc["hostname"].is<const char*>());
    TEST_ASSERT_TRUE(doc["checkInterval"].is<int>());
}

void test_api_config_post_success(void) {
    String payload = "{\"hostname\":\"new-host\",\"sntpServer\":\"pool.ntp.org\",\"offsetHours\":2,\"checkInterval\":30,\"discordWebhookURL\":\"https://discord.com/api/webhooks/123/xyz\",\"wifiSSID\":\"\",\"wifiPassword\":\"\"}";

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/config", HTTP_POST, &req, payload);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    TEST_ASSERT_TRUE(storage.fileExists(CONFIG_FILE_PATH));
    config.loadConfig();
    config_t cfg = config.getConfig();
    TEST_ASSERT_EQUAL_STRING("new-host", cfg.hostname.c_str());
    TEST_ASSERT_EQUAL_UINT8(30, cfg.checkInterval);
}

void test_api_config_post_forbidden(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;

    String payload = "{\"hostname\":\"new-host\"}";
    AsyncWebServerRequest req;
    testServer->dispatch("/api/config", HTTP_POST, &req, payload);
    TEST_ASSERT_EQUAL_INT(403, req.lastCode);
}

void test_api_config_backup(void) {
    // Setup files in storage
    storage.writeFile(CONFIG_FILE_PATH, "{\"hostname\":\"backup_test\"}");
    storage.writeFile(ENTITIES_FILE_PATH, "[{\"name\":\"E1\"}]");

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/config/backup", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    deserializeJson(doc, req.lastBody);
    TEST_ASSERT_TRUE(doc.containsKey("config"));
    TEST_ASSERT_TRUE(doc.containsKey("entities"));
    TEST_ASSERT_EQUAL_STRING("backup_test", doc["config"]["hostname"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("E1", doc["entities"][0]["name"].as<const char*>());
}

void test_api_config_restore(void) {
    String backupPayload = "{\"config\":{\"hostname\":\"restored_host\"},\"entities\":[{\"name\":\"RestoredE\"}]}";

    AsyncWebServerRequest req;
    bool handled = testServer->dispatchUpload("/api/config/restore", HTTP_POST, &req, "backup.json", backupPayload);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
    TEST_ASSERT_TRUE(shouldRestart);

    // Verify written to storage
    String savedConfig = storage.readFile(CONFIG_FILE_PATH);
    TEST_ASSERT_TRUE(savedConfig.indexOf("restored_host") >= 0);
    String savedEntities = storage.readFile(ENTITIES_FILE_PATH);
    TEST_ASSERT_TRUE(savedEntities.indexOf("RestoredE") >= 0);
}

void test_api_config_factory_reset(void) {
    storage.writeFile(CONFIG_FILE_PATH, "{\"test\":1}");
    storage.writeFile(ENTITIES_FILE_PATH, "[]");

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/config/factoryReset", HTTP_POST, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
    TEST_ASSERT_TRUE(shouldRestart);
    TEST_ASSERT_FALSE(storage.fileExists(CONFIG_FILE_PATH));
    TEST_ASSERT_FALSE(storage.fileExists(ENTITIES_FILE_PATH));
}

void test_api_wifiscan(void) {
    MockWiFiClass::mockScanResult = 1;

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/wifiscan", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    deserializeJson(doc, req.lastBody);
    TEST_ASSERT_EQUAL_STRING("done", doc["status"].as<const char*>());
}

void test_api_test_discord(void) {
    AsyncWebServerRequest reqMissing;
    testServer->dispatch("/api/testDiscord", HTTP_POST, &reqMissing);
    TEST_ASSERT_EQUAL_INT(400, reqMissing.lastCode);

    AsyncWebServerRequest reqValid;
    reqValid.setParam("url", "https://discord.com/api/webhooks/test");
    testServer->dispatch("/api/testDiscord", HTTP_POST, &reqValid);
    TEST_ASSERT_EQUAL_INT(200, reqValid.lastCode);
}

void test_api_summary(void) {
    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/summary", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    deserializeJson(doc, req.lastBody);
    TEST_ASSERT_TRUE(doc.containsKey("total"));
    TEST_ASSERT_TRUE(doc.containsKey("online"));
    TEST_ASSERT_TRUE(doc.containsKey("offline"));
}

void test_api_monitoredentities_get_all(void) {
    MonitoredEntity e1("Server1", "http://server1.local", "server", true);
    monitoredEntityRepository.addEntity(e1);
    MonitoredEntity e2("Server2", "http://server2.local", "router", false);
    monitoredEntityRepository.addEntity(e2);

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/monitoredentities", HTTP_GET, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    deserializeJson(doc, req.lastBody);
    TEST_ASSERT_TRUE(doc.is<JsonArray>());
    TEST_ASSERT_EQUAL_UINT(2, doc.as<JsonArray>().size());
}

void test_api_monitoredentity_crud(void) {
    // 1. Create (POST)
    String newEntityJson = "{\"name\":\"TestNode\",\"address\":\"http://node.local\",\"icon\":\"cube\",\"notification\":true,\"disabled\":false}";
    AsyncWebServerRequest reqPost;
    bool handled = testServer->dispatch("/api/monitoredentity", HTTP_POST, &reqPost, newEntityJson);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(201, reqPost.lastCode);

    // 2. Conflict on duplicate POST
    AsyncWebServerRequest reqPostDup;
    testServer->dispatch("/api/monitoredentity", HTTP_POST, &reqPostDup, newEntityJson);
    TEST_ASSERT_EQUAL_INT(409, reqPostDup.lastCode);

    // 3. Read (GET by name)
    AsyncWebServerRequest reqGet;
    handled = testServer->dispatch("/api/monitoredentity/TestNode", HTTP_GET, &reqGet);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, reqGet.lastCode);
    JsonDocument docGet;
    deserializeJson(docGet, reqGet.lastBody);
    TEST_ASSERT_EQUAL_STRING("TestNode", docGet["name"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("http://node.local", docGet["address"].as<const char*>());

    // 4. Update (PUT)
    String updateJson = "{\"name\":\"TestNode\",\"address\":\"http://updated-node.local\",\"icon\":\"server\",\"notification\":false,\"disabled\":true}";
    AsyncWebServerRequest reqPut;
    handled = testServer->dispatch("/api/monitoredentity/TestNode", HTTP_PUT, &reqPut, updateJson);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, reqPut.lastCode);

    MonitoredEntity updated;
    TEST_ASSERT_TRUE(monitoredEntityRepository.getEntity("TestNode", updated));
    TEST_ASSERT_EQUAL_STRING("http://updated-node.local", updated.getAddress().c_str());
    TEST_ASSERT_TRUE(updated.isDisabled());

    // 5. Delete (DELETE)
    AsyncWebServerRequest reqDel;
    handled = testServer->dispatch("/api/monitoredentity/TestNode", HTTP_DELETE, &reqDel);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, reqDel.lastCode);
    TEST_ASSERT_FALSE(monitoredEntityRepository.hasEntity("TestNode"));

    // 6. Delete non-existent -> 404
    AsyncWebServerRequest reqDel404;
    testServer->dispatch("/api/monitoredentity/TestNode", HTTP_DELETE, &reqDel404);
    TEST_ASSERT_EQUAL_INT(404, reqDel404.lastCode);
}

void test_api_ap_mode_excludes_service_endpoints(void) {
    delete testApi;
    delete testServer;

    isAPMode = true;
    testServer = new AsyncWebServer(80);
    testApi = new Api(*testServer);
    testApi->registerEndpoints();

    AsyncWebServerRequest req;
    // In AP mode, /api/summary is NOT registered
    bool handled = testServer->dispatch("/api/summary", HTTP_GET, &req);
    TEST_ASSERT_FALSE(handled);
}

void test_api_web_upload_archive_forbidden(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/web/upload-archive", HTTP_POST, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(403, req.lastCode);
}

void test_api_web_upload_archive_success(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;

    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/web/upload-archive", HTTP_POST, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
}

void test_api_config_test_led_forbidden(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;
    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/config/test-led", HTTP_POST, &req, "{\"brightness\":60}");
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(403, req.lastCode);
}

void test_api_config_test_led_success(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;
    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/config/test-led", HTTP_POST, &req, "{\"brightness\":70,\"duration\":3000}");
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
    TEST_ASSERT_TRUE(ledService.isTestActive());
    TEST_ASSERT_EQUAL_UINT8(70, ledService.getTestBrightness());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((255ULL * 70) / 100), ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((255ULL * 70) / 100), ledService.getLastB());
}

void test_api_config_test_led_empty_body(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;
    AsyncWebServerRequest req;
    bool handled = testServer->dispatch("/api/config/test-led", HTTP_POST, &req);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
    TEST_ASSERT_TRUE(ledService.isTestActive());
}

void test_api_ota_start_forbidden_and_authorized(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;

    AsyncWebServerRequest forbiddenReq;
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &forbiddenReq));
    TEST_ASSERT_EQUAL_INT(403, forbiddenReq.lastCode);

    AsyncWebServerRequest pioReq;
    pioReq.setHeader("X-PlatformIO-Upload", "1");
    pioReq.setParam("mode", "fr");
    pioReq.setParam("hash", "D41D8CD98F00B204E9800998ECF8427E");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &pioReq));
    TEST_ASSERT_EQUAL_INT(200, pioReq.lastCode);

    AsyncWebServerRequest badModeReq;
    badModeReq.setHeader("X-PlatformIO-Upload", "1");
    badModeReq.setParam("mode", "invalid");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &badModeReq));
    TEST_ASSERT_EQUAL_INT(400, badModeReq.lastCode);

    AsyncWebServerRequest badHashReq;
    badHashReq.setHeader("X-PlatformIO-Upload", "1");
    badHashReq.setParam("hash", "not-an-md5");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &badHashReq));
    TEST_ASSERT_EQUAL_INT(400, badHashReq.lastCode);
}

void test_api_ota_upload_forbidden(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;
    AsyncWebServerRequest req;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/ota/upload", HTTP_POST, &req, "firmware.bin", "BINDATA"));
    TEST_ASSERT_EQUAL_INT(403, req.lastCode);
    TEST_ASSERT_FALSE(shouldRestart);
    TEST_ASSERT_FALSE(isOtaUpdating);
}

void test_api_ota_upload_success_with_md5(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;

    AsyncWebServerRequest startReq;
    startReq.setParam("mode", "fr");
    startReq.setParam("hash", "0123456789ABCDEF0123456789ABCDEF");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &startReq));
    TEST_ASSERT_EQUAL_INT(200, startReq.lastCode);

    AsyncWebServerRequest uploadReq;
    const String firmwarePayload = "ESP32_FIRMWARE_BINARY_DATA";
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/ota/upload", HTTP_POST, &uploadReq, "firmware.bin", firmwarePayload));
    TEST_ASSERT_EQUAL_INT(200, uploadReq.lastCode);
    TEST_ASSERT_TRUE(shouldRestart);
    TEST_ASSERT_TRUE(isOtaUpdating);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", Update.expectedMd5.c_str());
    TEST_ASSERT_EQUAL_UINT32(firmwarePayload.length(), Update.writtenBytes.size());
}

void test_api_ota_upload_failure_restores_state(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;
    Update.nextBeginResult = false;
    Update.nextErrorString = "Partition Could Not be Found";

    AsyncWebServerRequest uploadReq;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/ota/upload", HTTP_POST, &uploadReq, "firmware.bin", "BINDATA"));
    TEST_ASSERT_EQUAL_INT(500, uploadReq.lastCode);
    TEST_ASSERT_FALSE(shouldRestart);
    TEST_ASSERT_FALSE(isOtaUpdating);
    TEST_ASSERT_TRUE(uploadReq.lastBody.indexOf("Partition Could Not be Found") >= 0);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_api_health);
    RUN_TEST(test_api_sysinfo);
    RUN_TEST(test_api_sysinfo_details);
    RUN_TEST(test_api_config_get);
    RUN_TEST(test_api_config_post_success);
    RUN_TEST(test_api_config_post_forbidden);
    RUN_TEST(test_api_config_backup);
    RUN_TEST(test_api_config_restore);
    RUN_TEST(test_api_config_factory_reset);
    RUN_TEST(test_api_wifiscan);
    RUN_TEST(test_api_test_discord);
    RUN_TEST(test_api_config_test_led_forbidden);
    RUN_TEST(test_api_config_test_led_success);
    RUN_TEST(test_api_config_test_led_empty_body);
    RUN_TEST(test_api_ota_start_forbidden_and_authorized);
    RUN_TEST(test_api_ota_upload_forbidden);
    RUN_TEST(test_api_ota_upload_success_with_md5);
    RUN_TEST(test_api_ota_upload_failure_restores_state);
    RUN_TEST(test_api_summary);
    RUN_TEST(test_api_monitoredentities_get_all);
    RUN_TEST(test_api_monitoredentity_crud);
    RUN_TEST(test_api_ap_mode_excludes_service_endpoints);
    RUN_TEST(test_api_web_upload_archive_forbidden);
    RUN_TEST(test_api_web_upload_archive_success);
    return UNITY_END();
}
