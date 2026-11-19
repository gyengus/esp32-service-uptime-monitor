#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoFake.h>
#include "WiFi.h"
#include "WsService.h"
#include "WsLogStream.h"
#include "Config.h"
#include "MonitoredEntityRepository.h"
#include "ESPAsyncWebServer.h"

using namespace fakeit;

#include "../mocks/MockGlobals.cpp"

static AsyncWebServer* testServer = nullptr;

void setUp(void) {
    MockWiFiClass::resetMock();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(123456);

    isConfigFunctionsEnabled = false;
    isAPMode = false;
    shouldRestart = false;

    // Reset repo
    auto entities = monitoredEntityRepository.getEntitiesSnapshot();
    for (auto& e : entities) {
        monitoredEntityRepository.removeEntity(e.getName());
    }

    if (testServer) delete testServer;
    testServer = new AsyncWebServer(80);
    wsService.init(*testServer);
    wsService.closeAll();
    wsService.setLogLevel(DEFAULT_LOG_LEVEL);
}

void tearDown(void) {
    wsService.closeAll();
    if (testServer) {
        delete testServer;
        testServer = nullptr;
    }
    ArduinoFakeReset();
}

void test_ws_initialization(void) {
    TEST_ASSERT_EQUAL_size_t(0, wsService.count());
    TEST_ASSERT_FALSE(wsService.hasActiveClients());
    TEST_ASSERT_EQUAL_size_t(1, testServer->handlers.size());
}

void test_ws_client_connect_sends_init_state(void) {
    MonitoredEntity entity("TestRouter", "http://192.168.1.1", "mdi-router", true);
    entity.setOnlineStatus(true);
    monitoredEntityRepository.addEntity(entity);

    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(10);
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_EQUAL_size_t(1, wsService.count());
    TEST_ASSERT_TRUE(wsService.hasActiveClients());

    TEST_ASSERT_TRUE(client->lastMessage.length() > 0);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, client->lastMessage);
    TEST_ASSERT_FALSE(err);
    TEST_ASSERT_EQUAL_STRING("init", doc["type"]);
    TEST_ASSERT_TRUE(doc.containsKey("entities"));
    TEST_ASSERT_EQUAL_size_t(1, doc["entities"].size());
    TEST_ASSERT_EQUAL_STRING("TestRouter", doc["entities"][0]["name"]);
    TEST_ASSERT_TRUE(doc["entities"][0]["isOnline"].as<bool>());
    TEST_ASSERT_TRUE(doc.containsKey("summary"));
    TEST_ASSERT_EQUAL_INT(1, doc["summary"]["total"]);
    TEST_ASSERT_EQUAL_INT(1, doc["summary"]["online"]);
    TEST_ASSERT_EQUAL_INT(0, doc["summary"]["offline"]);
    TEST_ASSERT_TRUE(doc.containsKey("sysinfo"));
}

void test_ws_broadcast_entity_update(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(11);
    client->sentMessages.clear();

    MonitoredEntity entity("TestSwitch", "http://192.168.1.2", "mdi-switch", false);
    entity.setOnlineStatus(false);
    wsService.broadcastEntityUpdate(entity);

    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    String last = client->sentMessages.back();

    JsonDocument doc;
    deserializeJson(doc, last);
    TEST_ASSERT_EQUAL_STRING("entity_update", doc["type"]);
    TEST_ASSERT_EQUAL_STRING("TestSwitch", doc["entity"]["name"]);
    TEST_ASSERT_FALSE(doc["entity"]["isOnline"].as<bool>());
}

void test_ws_broadcast_entity_deleted(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(12);
    client->sentMessages.clear();

    wsService.broadcastEntityDeleted("OldService");

    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("entity_delete", doc["type"]);
    TEST_ASSERT_EQUAL_STRING("OldService", doc["name"]);
}

void test_ws_broadcast_summary(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(13);
    client->sentMessages.clear();

    wsService.broadcastSummary(5, 2, 7);

    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("summary", doc["type"]);
    TEST_ASSERT_EQUAL_INT(7, doc["data"]["total"]);
    TEST_ASSERT_EQUAL_INT(5, doc["data"]["online"]);
    TEST_ASSERT_EQUAL_INT(2, doc["data"]["offline"]);
}

void test_ws_broadcast_sysinfo(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(14);
    client->sentMessages.clear();

    isConfigFunctionsEnabled = true;
    wsService.broadcastSysinfo();

    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("sysinfo", doc["type"]);
    TEST_ASSERT_TRUE(doc["data"]["uptime"].is<unsigned long>());
    TEST_ASSERT_TRUE(doc["data"]["freeHeap"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["data"]["maxFreeBlock"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["data"]["heapSize"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["data"]["wsClients"].is<size_t>());
    TEST_ASSERT_EQUAL_UINT(1, doc["data"]["wsClients"].as<size_t>());
    TEST_ASSERT_TRUE(doc["data"]["stackHighWaterBytes"].is<uint32_t>());
    TEST_ASSERT_TRUE(doc["data"]["configEnabled"].as<bool>());
    TEST_ASSERT_TRUE(doc["data"]["configTimeoutRemaining"].is<uint32_t>());
}

void test_ws_queue_and_process_events(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(15);
    client->sentMessages.clear();

    MonitoredEntity entity("QueueEntity", "http://10.0.0.1", "mdi-server", false);
    entity.setOnlineStatus(true);

    wsService.queueEntityUpdate(entity);
    wsService.queueSummary(3, 1, 4);
    wsService.queueEntityDeleted("DeletedQueueItem");

    // Before processEvents, no broadcasts were triggered by the queue
    TEST_ASSERT_EQUAL_size_t(0, client->sentMessages.size());

    // Process all queued events
    wsService.processEvents();

    TEST_ASSERT_EQUAL_size_t(3, client->sentMessages.size());

    JsonDocument doc1, doc2, doc3;
    deserializeJson(doc1, client->sentMessages[0]);
    TEST_ASSERT_EQUAL_STRING("entity_update", doc1["type"]);
    TEST_ASSERT_EQUAL_STRING("QueueEntity", doc1["entity"]["name"]);

    deserializeJson(doc2, client->sentMessages[1]);
    TEST_ASSERT_EQUAL_STRING("summary", doc2["type"]);
    TEST_ASSERT_EQUAL_INT(4, doc2["data"]["total"]);

    deserializeJson(doc3, client->sentMessages[2]);
    TEST_ASSERT_EQUAL_STRING("entity_delete", doc3["type"]);
    TEST_ASSERT_EQUAL_STRING("DeletedQueueItem", doc3["name"]);
}

void test_ws_log_subscribe_and_streaming(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(20);
    client->sentMessages.clear();

    TEST_ASSERT_FALSE(wsService.hasLogSubscribers());

    // Subscribe to logs
    String subMsg = "{\"type\":\"subscribe_logs\",\"enabled\":true}";
    wsService.getWebSocket().mockData(client, subMsg);
    TEST_ASSERT_TRUE(wsService.hasLogSubscribers());

    // Write log line to stream
    const char* logMsg = "2026-11-09 [INFO] System is healthy\n";
    wsLogStream.write((const uint8_t*)logMsg, strlen(logMsg));

    // Process queued events
    wsService.processEvents();

    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("log", doc["type"]);
    TEST_ASSERT_EQUAL_STRING("2026-11-09 [INFO] System is healthy", doc["line"]);

    // Unsubscribe
    String unsubMsg = "{\"type\":\"subscribe_logs\",\"enabled\":false}";
    wsService.getWebSocket().mockData(client, unsubMsg);
    TEST_ASSERT_FALSE(wsService.hasLogSubscribers());
}

void test_ws_set_log_level(void) {
    AsyncWebSocketClient* client = wsService.getWebSocket().mockConnect(21);
    client->sentMessages.clear();

    // 1. In AP mode: rejected because AP mode only serves config.html (no log viewer/modification)
    isAPMode = true;
    String setMsg = "{\"type\":\"set_log_level\",\"level\":\"WARNING\"}";
    wsService.getWebSocket().mockData(client, setMsg);
    TEST_ASSERT_EQUAL_UINT8(DEFAULT_LOG_LEVEL, config.getConfig().logLevel);
    TEST_ASSERT_EQUAL_size_t(0, client->sentMessages.size());
    isAPMode = false;

#if defined(RELEASE) || defined(DISABLE_DEBUG_LOGGING)
    // In release mode: set_log_level is rejected and returns INFO (7)
    wsService.getWebSocket().mockData(client, setMsg);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_INFO, config.getConfig().logLevel);
    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("log_level", doc["type"]);
    TEST_ASSERT_EQUAL_UINT8(ELOG_LEVEL_INFO, doc["level"].as<uint8_t>());
    TEST_ASSERT_EQUAL_STRING("INFO", doc["name"]);
#else
    // 2. In normal STA mode (dev build): set_log_level is accepted
    wsService.getWebSocket().mockData(client, setMsg);
    TEST_ASSERT_EQUAL_UINT8(5, config.getConfig().logLevel);
    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("log_level", doc["type"]);
    TEST_ASSERT_EQUAL_UINT8(5, doc["level"].as<uint8_t>());
    TEST_ASSERT_EQUAL_STRING("WARNING", doc["name"]);

    // 3. Switch back to DEBUG: fully accepted in dev mode
    client->sentMessages.clear();
    String setDebugMsg = "{\"type\":\"set_log_level\",\"level\":\"DEBUG\"}";
    wsService.getWebSocket().mockData(client, setDebugMsg);
    TEST_ASSERT_EQUAL_UINT8(8, config.getConfig().logLevel);
    TEST_ASSERT_TRUE(client->sentMessages.size() > 0);
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("log_level", doc["type"]);
    TEST_ASSERT_EQUAL_UINT8(8, doc["level"].as<uint8_t>());
    TEST_ASSERT_EQUAL_STRING("DEBUG", doc["name"]);
#endif
}

void test_ws_log_subscriber_limit(void) {
    wsService.closeAll();
    TEST_ASSERT_FALSE(wsService.hasLogSubscribers());

    auto c1 = wsService.getWebSocket().mockConnect(31);
    auto c2 = wsService.getWebSocket().mockConnect(32);
    auto c3 = wsService.getWebSocket().mockConnect(33);
    auto c4 = wsService.getWebSocket().mockConnect(34);

    c1->sentMessages.clear();
    c2->sentMessages.clear();
    c3->sentMessages.clear();
    c4->sentMessages.clear();

    String subMsg = "{\"type\":\"subscribe_logs\",\"enabled\":true}";
    wsService.getWebSocket().mockData(c1, subMsg);
    wsService.getWebSocket().mockData(c2, subMsg);
    wsService.getWebSocket().mockData(c3, subMsg);
    // 4th subscriber attempt should be rejected due to MAX 3 limit
    wsService.getWebSocket().mockData(c4, subMsg);

    // Verify only 3 are subscribed by sending a log message
    const char* logMsg = "2026-11-14 [INFO] Hello subscribers\n";
    wsLogStream.write((const uint8_t*)logMsg, strlen(logMsg));
    wsService.processEvents();

    TEST_ASSERT_EQUAL_size_t(1, c1->sentMessages.size());
    TEST_ASSERT_EQUAL_size_t(1, c2->sentMessages.size());
    TEST_ASSERT_EQUAL_size_t(1, c3->sentMessages.size());
    TEST_ASSERT_EQUAL_size_t(0, c4->sentMessages.size());
}

void test_ws_debug_log_filtered_without_config_mode(void) {
    wsService.closeAll();
    auto client = wsService.getWebSocket().mockConnect(40);
    client->sentMessages.clear();

    String subMsg = "{\"type\":\"subscribe_logs\",\"enabled\":true}";
    wsService.getWebSocket().mockData(client, subMsg);

    isConfigFunctionsEnabled = false;
    isAPMode = false;

    const char* debugMsg = "2026-11-14 [DEBUG] Internal detail message\n";
    wsLogStream.write((const uint8_t*)debugMsg, strlen(debugMsg));
    wsService.processEvents();
#if defined(RELEASE) || defined(DISABLE_DEBUG_LOGGING)
    TEST_ASSERT_EQUAL_size_t(0, client->sentMessages.size());
#else
    TEST_ASSERT_EQUAL_size_t(1, client->sentMessages.size());
#endif

    // INFO log should also pass
    const char* infoMsg = "2026-11-14 [INFO] Normal status message\n";
    wsLogStream.write((const uint8_t*)infoMsg, strlen(infoMsg));
    wsService.processEvents();
#if defined(RELEASE) || defined(DISABLE_DEBUG_LOGGING)
    TEST_ASSERT_EQUAL_size_t(1, client->sentMessages.size());
#else
    TEST_ASSERT_EQUAL_size_t(2, client->sentMessages.size());
#endif
}

void test_ws_handshake_origin_validation(void) {
    AsyncWebServerRequest evilReq;
    evilReq.setHeader("Host", "localhost");
    evilReq.setHeader("Origin", "http://evil.com");
    auto evilClient = wsService.getWebSocket().mockConnect(50, &evilReq);
    TEST_ASSERT_NULL(evilClient);

    AsyncWebServerRequest validReq;
    validReq.setHeader("Host", "localhost");
    validReq.setHeader("Origin", "http://localhost");
    auto validClient = wsService.getWebSocket().mockConnect(51, &validReq);
    TEST_ASSERT_NOT_NULL(validClient);

    AsyncWebServerRequest noOriginReq;
    noOriginReq.setHeader("Host", "localhost");
    auto noOriginClient = wsService.getWebSocket().mockConnect(52, &noOriginReq);
    TEST_ASSERT_NOT_NULL(noOriginClient);
}

void test_ws_log_stream_operations(void) {
    wsLogStream.clearBuffer();

    // 1. Null buffer and zero size
    TEST_ASSERT_EQUAL_size_t(0, wsLogStream.write(nullptr, 10));
    TEST_ASSERT_EQUAL_size_t(0, wsLogStream.write((const uint8_t*)"test", 0));

    // 2. Single character writes and CRLF handling
    auto client = wsService.getWebSocket().mockConnect(60);
    client->sentMessages.clear();
    String subMsg = "{\"type\":\"subscribe_logs\",\"enabled\":true}";
    wsService.getWebSocket().mockData(client, subMsg);

    wsLogStream.write('H');
    wsLogStream.write('i');
    wsLogStream.write('\r'); // Ignored in buffer
    wsLogStream.write('\n'); // Commits line
    wsService.processEvents();

    TEST_ASSERT_EQUAL_size_t(1, client->sentMessages.size());
    JsonDocument doc;
    deserializeJson(doc, client->sentMessages.back());
    TEST_ASSERT_EQUAL_STRING("Hi", doc["line"]);

    // 3. Clear buffer
    wsLogStream.clearBuffer();

    // 4. Buffer overflow handling (> 512 characters without \n)
    client->sentMessages.clear();
    char longLine[600];
    memset(longLine, 'A', sizeof(longLine) - 2);
    longLine[sizeof(longLine) - 2] = '\n';
    longLine[sizeof(longLine) - 1] = '\0';

    wsLogStream.write((const uint8_t*)longLine, sizeof(longLine) - 1);
    wsService.processEvents();

    TEST_ASSERT_TRUE(client->sentMessages.size() >= 1);
    deserializeJson(doc, client->sentMessages[0]);
    String line = doc["line"].as<String>();
    TEST_ASSERT_TRUE(line.endsWith("..."));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_ws_initialization);
    RUN_TEST(test_ws_client_connect_sends_init_state);
    RUN_TEST(test_ws_broadcast_entity_update);
    RUN_TEST(test_ws_broadcast_entity_deleted);
    RUN_TEST(test_ws_broadcast_summary);
    RUN_TEST(test_ws_broadcast_sysinfo);
    RUN_TEST(test_ws_queue_and_process_events);
    RUN_TEST(test_ws_log_subscribe_and_streaming);
    RUN_TEST(test_ws_set_log_level);
    RUN_TEST(test_ws_log_subscriber_limit);
    RUN_TEST(test_ws_debug_log_filtered_without_config_mode);
    RUN_TEST(test_ws_handshake_origin_validation);
    RUN_TEST(test_ws_log_stream_operations);
    return UNITY_END();
}
