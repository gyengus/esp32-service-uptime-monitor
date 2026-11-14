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

static uint32_t currentTestTime = 1000000;

void setUp(void) {
    storage.clear();
    MockWiFiClass::resetMock();
    Update.resetMock();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    currentTestTime += 100000;
    When(Method(ArduinoFake(), millis)).AlwaysReturn(currentTestTime);

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
    TEST_ASSERT_TRUE(doc["configEnabled"].as<bool>());
    TEST_ASSERT_TRUE(doc["configTimeoutRemaining"].is<uint32_t>());
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

    // Wi-Fi credentials change triggers networkContext.connect
    String wifiPayload = "{\"hostname\":\"new-host\",\"wifiSSID\":\"NewSSID\",\"wifiPassword\":\"NewPass\"}";
    AsyncWebServerRequest reqWifi;
    reqWifi.setHeader("Content-Type", "application/json");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config", HTTP_POST, &reqWifi, wifiPayload));
    TEST_ASSERT_EQUAL_INT(200, reqWifi.lastCode);

    // Wi-Fi connection failure returns 400
    MockWiFiClass::mockConnectFails = true;
    AsyncWebServerRequest reqWifiFail;
    reqWifiFail.setHeader("Content-Type", "application/json");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config", HTTP_POST, &reqWifiFail, "{\"wifiSSID\":\"FailSSID\",\"wifiPassword\":\"Pass\"}"));
    TEST_ASSERT_EQUAL_INT(400, reqWifiFail.lastCode);
    MockWiFiClass::mockConnectFails = false;
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
    TEST_ASSERT_EQUAL_STRING("no-store", req.lastCacheControl.c_str());

    JsonDocument doc;
    deserializeJson(doc, req.lastBody);
    TEST_ASSERT_TRUE(doc.containsKey("config"));
    TEST_ASSERT_TRUE(doc.containsKey("entities"));
    TEST_ASSERT_EQUAL_STRING("backup_test", doc["config"]["hostname"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("E1", doc["entities"][0]["name"].as<const char*>());
}

void test_api_config_restore(void) {
    String backupPayload = "{\"config\":{\"hostname\":\"restored-host\"},\"entities\":[{\"name\":\"RestoredE\",\"address\":\"http://192.168.1.50\",\"icon\":\"mdi-server\"}]}";

    AsyncWebServerRequest req;
    bool handled = testServer->dispatchUpload("/api/config/restore", HTTP_POST, &req, "backup.json", backupPayload);
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);
    TEST_ASSERT_TRUE(shouldRestart);

    // Verify written to storage
    String savedConfig = storage.readFile(CONFIG_FILE_PATH);
    TEST_ASSERT_TRUE(savedConfig.indexOf("restored-host") >= 0);
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

    // 7. URL-encoded name CRUD with spaces (%20)
    String spaceEntityJson = "{\"name\":\"Home Router\",\"address\":\"http://router.home\",\"icon\":\"router\",\"notification\":true,\"disabled\":false}";
    AsyncWebServerRequest reqPostSpace;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity", HTTP_POST, &reqPostSpace, spaceEntityJson));
    TEST_ASSERT_EQUAL_INT(201, reqPostSpace.lastCode);

    AsyncWebServerRequest reqGetSpace;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/Home%20Router", HTTP_GET, &reqGetSpace));
    TEST_ASSERT_EQUAL_INT(200, reqGetSpace.lastCode);

    AsyncWebServerRequest reqDelSpace;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/Home%20Router", HTTP_DELETE, &reqDelSpace));
    TEST_ASSERT_EQUAL_INT(200, reqDelSpace.lastCode);
    TEST_ASSERT_FALSE(monitoredEntityRepository.hasEntity("Home Router"));
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
    MonitoringTaskHandle = (TaskHandle_t)0x1234;

    AsyncWebServerRequest req;
    bool handled = testServer->dispatchUpload("/api/web/upload-archive", HTTP_POST, &req, "web.zip", "ZIPDATA");
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    MonitoringTaskHandle = nullptr;
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

    // Request with arbitrary header without configured token is forbidden
    AsyncWebServerRequest forbiddenPioReq;
    forbiddenPioReq.setHeader("X-PlatformIO-Upload", "1");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &forbiddenPioReq));
    TEST_ASSERT_EQUAL_INT(403, forbiddenPioReq.lastCode);

    // Configure OTA token
    config_t cfg = config.getConfig();
    cfg.otaToken = "test_ota_secret_token";
    config.saveConfig(cfg);

    // Request with wrong token is forbidden
    AsyncWebServerRequest wrongTokenReq;
    wrongTokenReq.setHeader("X-PlatformIO-Upload", "wrong_token");
    wrongTokenReq.setParam("mode", "fr");
    wrongTokenReq.setParam("hash", "D41D8CD98F00B204E9800998ECF8427E");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &wrongTokenReq));
    TEST_ASSERT_EQUAL_INT(403, wrongTokenReq.lastCode);

    // Request with valid token is authorized
    AsyncWebServerRequest pioReq;
    pioReq.setHeader("X-PlatformIO-Upload", "test_ota_secret_token");
    pioReq.setParam("mode", "fr");
    pioReq.setParam("hash", "D41D8CD98F00B204E9800998ECF8427E");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &pioReq));
    TEST_ASSERT_EQUAL_INT(200, pioReq.lastCode);

    AsyncWebServerRequest badModeReq;
    badModeReq.setHeader("X-PlatformIO-Upload", "test_ota_secret_token");
    badModeReq.setParam("mode", "invalid");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &badModeReq));
    TEST_ASSERT_EQUAL_INT(400, badModeReq.lastCode);

    AsyncWebServerRequest badHashReq;
    badHashReq.setHeader("X-PlatformIO-Upload", "test_ota_secret_token");
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
    MonitoringTaskHandle = (TaskHandle_t)0x5678;

    AsyncWebServerRequest startReq;
    startReq.setParam("mode", "fr");
    startReq.setParam("hash", "0123456789ABCDEF0123456789ABCDEF");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &startReq));
    TEST_ASSERT_EQUAL_INT(200, startReq.lastCode);

    AsyncWebServerRequest uploadReq;
    uploadReq.setParam("MD5", "0123456789ABCDEF0123456789ABCDEF");
    const String firmwarePayload = "ESP32_FIRMWARE_BINARY_DATA";
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/ota/upload", HTTP_POST, &uploadReq, "firmware.bin", firmwarePayload));
    TEST_ASSERT_EQUAL_INT(200, uploadReq.lastCode);
    TEST_ASSERT_TRUE(shouldRestart);
    TEST_ASSERT_TRUE(isOtaUpdating);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", Update.expectedMd5.c_str());
    TEST_ASSERT_EQUAL_UINT32(firmwarePayload.length(), Update.writtenBytes.size());

    MonitoringTaskHandle = nullptr;
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

    // Empty firmware binary
    Update.nextBeginResult = true;
    AsyncWebServerRequest emptyReq;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/ota/upload", HTTP_POST, &emptyReq, "firmware.bin", ""));
    TEST_ASSERT_EQUAL_INT(500, emptyReq.lastCode);
    TEST_ASSERT_TRUE(emptyReq.lastBody.indexOf("Empty firmware binary") >= 0);
}

void test_api_csrf_and_dns_rebinding_protection(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;

    // 1. Cross-site Origin mismatch on mutating endpoint should be 403 Forbidden
    AsyncWebServerRequest csrfReq;
    csrfReq.setHeader("Host", "localhost");
    csrfReq.setHeader("Origin", "http://evil.com");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config/factoryReset", HTTP_POST, &csrfReq));
    TEST_ASSERT_EQUAL_INT(403, csrfReq.lastCode);

    // 2. DNS rebinding host mismatch should be 403 Forbidden
    AsyncWebServerRequest rebindReq;
    rebindReq.setHeader("Host", "rebind.attacker.com");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config/factoryReset", HTTP_POST, &rebindReq));
    TEST_ASSERT_EQUAL_INT(403, rebindReq.lastCode);

    // 3. Same-origin request succeeds
    AsyncWebServerRequest validReq;
    validReq.setHeader("Host", "localhost");
    validReq.setHeader("Origin", "http://localhost");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config/factoryReset", HTTP_POST, &validReq));
    TEST_ASSERT_EQUAL_INT(200, validReq.lastCode);
}

void test_api_content_type_enforcement(void) {
    isConfigFunctionsEnabled = true;
    isAPMode = false;

    // Sending JSON payload as text/plain should return 415 Unsupported Media Type
    AsyncWebServerRequest textReq;
    textReq.setHeader("Host", "localhost");
    textReq.setHeader("Content-Type", "text/plain");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config", HTTP_POST, &textReq, "{\"checkInterval\":30}"));
    TEST_ASSERT_EQUAL_INT(415, textReq.lastCode);

    // Sending entity payload as form-urlencoded should return 415
    AsyncWebServerRequest formReq;
    formReq.setHeader("Host", "localhost");
    formReq.setHeader("Content-Type", "application/x-www-form-urlencoded");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity", HTTP_POST, &formReq, "{\"name\":\"Node\"}"));
    TEST_ASSERT_EQUAL_INT(415, formReq.lastCode);
}

void test_api_ota_token_rate_limiting_and_ap_mode(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;

    config_t cfg = config.getConfig();
    cfg.otaToken = "super_secret_ota_token";
    config.saveConfig(cfg);

    // 1. AP mode disables OTA uploads completely
    isAPMode = true;
    AsyncWebServerRequest apReq;
    apReq.setHeader("X-PlatformIO-Upload", "super_secret_ota_token");
    apReq.setParam("mode", "fr");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &apReq));
    TEST_ASSERT_EQUAL_INT(403, apReq.lastCode);

    isAPMode = false;

    // 2. 5 consecutive failed token attempts triggers 60s lockout
    for (int i = 0; i < 5; i++) {
        AsyncWebServerRequest wrongReq;
        wrongReq.setHeader("X-PlatformIO-Upload", "invalid_token");
        wrongReq.setParam("mode", "fr");
        TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &wrongReq));
        TEST_ASSERT_EQUAL_INT(403, wrongReq.lastCode);
    }

    // 6th attempt with valid token should still be blocked due to lockout
    AsyncWebServerRequest lockedReq;
    lockedReq.setHeader("X-PlatformIO-Upload", "super_secret_ota_token");
    lockedReq.setParam("mode", "fr");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &lockedReq));
    TEST_ASSERT_EQUAL_INT(403, lockedReq.lastCode);

    // 7th attempt after 60s lockout expires should succeed with valid token
    currentTestTime += 70000;
    When(Method(ArduinoFake(), millis)).AlwaysReturn(currentTestTime);
    AsyncWebServerRequest unlockedReq;
    unlockedReq.setHeader("X-PlatformIO-Upload", "super_secret_ota_token");
    unlockedReq.setParam("mode", "fr");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &unlockedReq));
    TEST_ASSERT_EQUAL_INT(200, unlockedReq.lastCode);
}

void test_api_config_get_masking(void) {
    config_t cfg = config.getConfig();
    cfg.wifiPassword = "my_secret_wifi_password";
    cfg.otaToken = "my_secret_ota_token_123";
    cfg.discordWebhookURL = "https://discord.com/api/webhooks/987654321/tokensecret";
    config.saveConfig(cfg);

    AsyncWebServerRequest req;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config", HTTP_GET, &req));
    TEST_ASSERT_EQUAL_INT(200, req.lastCode);

    JsonDocument doc;
    deserializeJson(doc, req.lastBody);
    TEST_ASSERT_EQUAL_STRING("******", doc["wifiPassword"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("******", doc["otaToken"].as<const char*>());
    String webhook = doc["discordWebhookURL"].as<String>();
    TEST_ASSERT_TRUE(webhook.endsWith("/******"));
}

void test_api_config_post_errors(void) {
    isConfigFunctionsEnabled = true;

    // 1. Invalid JSON body
    AsyncWebServerRequest reqInvalid;
    reqInvalid.setHeader("Content-Type", "application/json");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config", HTTP_POST, &reqInvalid, "{invalid_json:"));
    TEST_ASSERT_EQUAL_INT(400, reqInvalid.lastCode);

    // 2. Empty body
    AsyncWebServerRequest reqEmpty;
    reqEmpty.setHeader("Content-Type", "application/json");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config", HTTP_POST, &reqEmpty, ""));
    TEST_ASSERT_EQUAL_INT(400, reqEmpty.lastCode);
}

void test_api_config_restore_validation_errors(void) {
    isConfigFunctionsEnabled = true;

    // 1. Invalid JSON
    AsyncWebServerRequest req1;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/api/config/restore", HTTP_POST, &req1, "bad.json", "not a json string"));
    TEST_ASSERT_EQUAL_INT(400, req1.lastCode);

    // 2. Missing config or entities
    AsyncWebServerRequest req2;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/api/config/restore", HTTP_POST, &req2, "bad.json", "{\"config\":{}}"));
    TEST_ASSERT_EQUAL_INT(400, req2.lastCode);

    // 3. Invalid structure (entities is not array)
    AsyncWebServerRequest req3;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/api/config/restore", HTTP_POST, &req3, "bad.json", "{\"config\":{}, \"entities\":\"string\"}"));
    TEST_ASSERT_EQUAL_INT(400, req3.lastCode);

    // 4. Restore with default fallbacks and skipped invalid entity
    String restorePayload = "{\"config\":{}, \"entities\":[{\"name\":\"InvalidEntity\"}, {\"name\":\"ValidEntity\",\"address\":\"http://valid.local\",\"icon\":\"cube\"}]}";
    AsyncWebServerRequest req4;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/api/config/restore", HTTP_POST, &req4, "backup.json", restorePayload));
    TEST_ASSERT_EQUAL_INT(200, req4.lastCode);
}

void test_api_factory_reset_and_backup_forbidden(void) {
    isConfigFunctionsEnabled = false;
    isAPMode = false;

    AsyncWebServerRequest reqReset;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config/factoryReset", HTTP_POST, &reqReset));
    TEST_ASSERT_EQUAL_INT(403, reqReset.lastCode);

    AsyncWebServerRequest reqBackup;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/config/backup", HTTP_GET, &reqBackup));
    TEST_ASSERT_EQUAL_INT(403, reqBackup.lastCode);

    AsyncWebServerRequest reqRestore;
    TEST_ASSERT_TRUE(testServer->dispatchUpload("/api/config/restore", HTTP_POST, &reqRestore, "backup.json", "{}"));
    TEST_ASSERT_EQUAL_INT(403, reqRestore.lastCode);
}

void test_api_test_discord_edge_cases(void) {
    isConfigFunctionsEnabled = true;

    // 1. Invalid webhook URL -> 400
    AsyncWebServerRequest reqInvalid;
    reqInvalid.setParam("url", "http://127.0.0.1/webhook");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/testDiscord", HTTP_POST, &reqInvalid));
    TEST_ASSERT_EQUAL_INT(400, reqInvalid.lastCode);

    // 2. Default webhook URL from config used when url param is masked
    config_t cfg = config.getConfig();
    cfg.discordWebhookURL = "https://discord.com/api/webhooks/from-config";
    config.saveConfig(cfg);

    AsyncWebServerRequest reqDefault;
    reqDefault.setParam("url", "******");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/testDiscord", HTTP_POST, &reqDefault));
    TEST_ASSERT_EQUAL_INT(200, reqDefault.lastCode);

    // 3. Forbidden when config mode disabled
    isConfigFunctionsEnabled = false;
    AsyncWebServerRequest reqForbidden;
    reqForbidden.setParam("url", "https://discord.com/api/webhooks/1");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/testDiscord", HTTP_POST, &reqForbidden));
    TEST_ASSERT_EQUAL_INT(403, reqForbidden.lastCode);
}

void test_api_monitoredentity_errors_and_rename(void) {
    isConfigFunctionsEnabled = true;

    // 1. POST invalid JSON -> 400
    AsyncWebServerRequest reqInvalidJson;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity", HTTP_POST, &reqInvalidJson, "{malformed"));
    TEST_ASSERT_EQUAL_INT(400, reqInvalidJson.lastCode);

    // 2. POST missing address -> 400
    AsyncWebServerRequest reqMissingAddr;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity", HTTP_POST, &reqMissingAddr, "{\"name\":\"NoAddr\"}"));
    TEST_ASSERT_EQUAL_INT(400, reqMissingAddr.lastCode);

    // 3. GET nonexistent entity -> 404
    AsyncWebServerRequest reqGet404;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/NonExistentNode", HTTP_GET, &reqGet404));
    TEST_ASSERT_EQUAL_INT(404, reqGet404.lastCode);

    // 4. PUT nonexistent entity -> 404
    AsyncWebServerRequest reqPut404;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/NonExistentNode", HTTP_PUT, &reqPut404, "{\"name\":\"NonExistentNode\",\"address\":\"http://node.local\"}"));
    TEST_ASSERT_EQUAL_INT(404, reqPut404.lastCode);

    // 5. PUT rename existing entity
    MonitoredEntity initial("OriginalNode", "http://original.local", "cube", true);
    monitoredEntityRepository.addEntity(initial);

    AsyncWebServerRequest reqRename;
    String renameJson = "{\"name\":\"RenamedNode\",\"address\":\"http://renamed.local\",\"icon\":\"cube\"}";
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/OriginalNode", HTTP_PUT, &reqRename, renameJson));
    TEST_ASSERT_EQUAL_INT(200, reqRename.lastCode);
    TEST_ASSERT_FALSE(monitoredEntityRepository.hasEntity("OriginalNode"));
    TEST_ASSERT_TRUE(monitoredEntityRepository.hasEntity("RenamedNode"));

    // 6. URL decode with '+'
    AsyncWebServerRequest reqPlusSpace;
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/Plus+Name", HTTP_GET, &reqPlusSpace));
    TEST_ASSERT_EQUAL_INT(404, reqPlusSpace.lastCode);

    // 7. POST and PUT forbidden when config mode is disabled
    isConfigFunctionsEnabled = false;
    AsyncWebServerRequest reqPostForbidden;
    reqPostForbidden.setHeader("Content-Type", "application/json");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity", HTTP_POST, &reqPostForbidden, "{\"name\":\"Node\",\"address\":\"http://a\"}"));
    TEST_ASSERT_EQUAL_INT(403, reqPostForbidden.lastCode);

    AsyncWebServerRequest reqPutForbidden;
    reqPutForbidden.setHeader("Content-Type", "application/json");
    TEST_ASSERT_TRUE(testServer->dispatch("/api/monitoredentity/Node", HTTP_PUT, &reqPutForbidden, "{\"name\":\"Node\",\"address\":\"http://a\"}"));
    TEST_ASSERT_EQUAL_INT(403, reqPutForbidden.lastCode);
    isConfigFunctionsEnabled = true;
}

void test_api_ota_start_already_in_progress(void) {
    isConfigFunctionsEnabled = true;

    config_t cfg = config.getConfig();
    cfg.otaToken = "super_secret_ota_token";
    config.saveConfig(cfg);

    // 1. Invalid MD5 hash parameter
    AsyncWebServerRequest reqBadMd5;
    reqBadMd5.setHeader("X-PlatformIO-Upload", "super_secret_ota_token");
    reqBadMd5.setParam("mode", "fr");
    reqBadMd5.setParam("hash", "invalid_md5_hash_with_non_hex_chars!!");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &reqBadMd5));
    TEST_ASSERT_EQUAL_INT(400, reqBadMd5.lastCode);

    // 2. OTA update already in progress -> 409
    isOtaUpdating = true;
    AsyncWebServerRequest req;
    req.setHeader("X-PlatformIO-Upload", "super_secret_ota_token");
    req.setParam("mode", "fr");
    TEST_ASSERT_TRUE(testServer->dispatch("/ota/start", HTTP_GET, &req));
    TEST_ASSERT_EQUAL_INT(409, req.lastCode);

    isOtaUpdating = false;
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_api_health);
    RUN_TEST(test_api_sysinfo);
    RUN_TEST(test_api_sysinfo_details);
    RUN_TEST(test_api_config_get);
    RUN_TEST(test_api_config_get_masking);
    RUN_TEST(test_api_config_post_success);
    RUN_TEST(test_api_config_post_forbidden);
    RUN_TEST(test_api_config_post_errors);
    RUN_TEST(test_api_config_backup);
    RUN_TEST(test_api_config_restore);
    RUN_TEST(test_api_config_restore_validation_errors);
    RUN_TEST(test_api_config_factory_reset);
    RUN_TEST(test_api_factory_reset_and_backup_forbidden);
    RUN_TEST(test_api_wifiscan);
    RUN_TEST(test_api_test_discord);
    RUN_TEST(test_api_test_discord_edge_cases);
    RUN_TEST(test_api_config_test_led_forbidden);
    RUN_TEST(test_api_config_test_led_success);
    RUN_TEST(test_api_config_test_led_empty_body);
    RUN_TEST(test_api_ota_start_forbidden_and_authorized);
    RUN_TEST(test_api_ota_start_already_in_progress);
    RUN_TEST(test_api_ota_upload_forbidden);
    RUN_TEST(test_api_ota_upload_success_with_md5);
    RUN_TEST(test_api_ota_upload_failure_restores_state);
    RUN_TEST(test_api_summary);
    RUN_TEST(test_api_monitoredentities_get_all);
    RUN_TEST(test_api_monitoredentity_crud);
    RUN_TEST(test_api_monitoredentity_errors_and_rename);
    RUN_TEST(test_api_ap_mode_excludes_service_endpoints);
    RUN_TEST(test_api_web_upload_archive_forbidden);
    RUN_TEST(test_api_web_upload_archive_success);
    RUN_TEST(test_api_csrf_and_dns_rebinding_protection);
    RUN_TEST(test_api_content_type_enforcement);
    RUN_TEST(test_api_ota_token_rate_limiting_and_ap_mode);
    return UNITY_END();
}
