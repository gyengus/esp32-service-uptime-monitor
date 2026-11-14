#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoFake.h>
#include "NetworkContext.h"
#include "Config.h"
#include "Storage.h"
#include "WiFi.h"

using namespace fakeit;

#include "../mocks/MockGlobals.cpp"

void setUp(void) {
    storage.clear();
    MockWiFiClass::resetMock();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
}

void tearDown(void) {
    ArduinoFakeReset();
    storage.clear();
    MockWiFiClass::resetMock();
}

void test_network_context_connect_success(void) {
    MockWiFiClass::mockStatus = WL_CONNECTED;

    TEST_ASSERT_TRUE(networkContext.connect("TestSSID", "secretPass"));
    TEST_ASSERT_EQUAL_STRING("TestSSID", MockWiFiClass::lastSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("secretPass", MockWiFiClass::lastPassword.c_str());
    TEST_ASSERT_EQUAL_INT(WIFI_STA, MockWiFiClass::mockMode);
    TEST_ASSERT_TRUE(networkContext.isConnected());
}

void test_network_context_disconnect(void) {
    networkContext.connect("SSID1", "pass1");
    TEST_ASSERT_TRUE(networkContext.isConnected());

    networkContext.disconnect();
    TEST_ASSERT_FALSE(networkContext.isConnected());
    TEST_ASSERT_EQUAL_INT(WIFI_OFF, MockWiFiClass::mockMode);
}

void test_network_context_ap_mode_start_and_stop(void) {
    networkContext.startAPMode();
    TEST_ASSERT_EQUAL_INT(WIFI_AP, MockWiFiClass::mockMode);

    networkContext.stopAPMode();
}

void test_network_context_get_ip(void) {
    // When connected in STA mode, returns localIP
    MockWiFiClass::mockStatus = WL_CONNECTED;
    MockWiFiClass::mockMode = WIFI_STA;
    MockWiFiClass::mockIP = MockIPAddress(192, 168, 1, 88);
    TEST_ASSERT_EQUAL_STRING("192.168.1.88", networkContext.getIP().c_str());

    // When disconnected, returns empty string
    MockWiFiClass::mockStatus = WL_DISCONNECTED;
    TEST_ASSERT_EQUAL_STRING("", networkContext.getIP().c_str());

    // When connected in AP mode, returns softAPIP
    MockWiFiClass::mockStatus = WL_CONNECTED;
    MockWiFiClass::mockMode = WIFI_AP;
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", networkContext.getIP().c_str());
}

void test_network_context_check_connection(void) {
    networkContext.connect("HomeWiFi", "pass123");
    TEST_ASSERT_TRUE(networkContext.isConnected());

    // While connected, checkConnection maintains connection
    networkContext.checkConnection();
    TEST_ASSERT_TRUE(networkContext.isConnected());

    // When connection drops in STA mode, checkConnection triggers reconnect
    MockWiFiClass::mockStatus = WL_DISCONNECTED;
    TEST_ASSERT_FALSE(networkContext.isConnected());

    networkContext.checkConnection();
    TEST_ASSERT_TRUE(networkContext.isConnected());
    TEST_ASSERT_EQUAL_STRING("HomeWiFi", MockWiFiClass::lastSSID.c_str());
}

void test_network_context_scan_networks_done(void) {
    MockWiFiClass::mockScanResult = 2; // Found 2 networks

    JsonDocument doc;
    networkContext.scanNetworks(doc);

    TEST_ASSERT_EQUAL_STRING("done", doc["status"].as<const char*>());
    TEST_ASSERT_TRUE(doc["networks"].is<JsonArray>());

    JsonArray networks = doc["networks"].as<JsonArray>();
    TEST_ASSERT_EQUAL_UINT(2, networks.size());
    TEST_ASSERT_EQUAL_STRING("WiFiNetwork_0", networks[0]["ssid"].as<const char*>());
    TEST_ASSERT_EQUAL_INT(-65, networks[0]["rssi"].as<int>());
}

void test_network_context_scan_networks_running(void) {
    MockWiFiClass::mockScanResult = WIFI_SCAN_RUNNING; // -1

    JsonDocument doc;
    networkContext.scanNetworks(doc);

    TEST_ASSERT_EQUAL_STRING("in progress", doc["status"].as<const char*>());
}

void test_network_context_scan_networks_trigger(void) {
    MockWiFiClass::mockScanResult = WIFI_SCAN_FAILED; // -2

    JsonDocument doc;
    networkContext.scanNetworks(doc);

    TEST_ASSERT_EQUAL_STRING("scanning", doc["status"].as<const char*>());
}

void test_network_context_hostname_and_logger(void) {
    networkContext.setHostname("custom-device");
    TEST_ASSERT_EQUAL_STRING("custom-device", networkContext.getHostname().c_str());

    static String lastLogLevel;
    static String lastLogMsg;
    networkContext.setLogger([](const char* level, const char* msg) {
        lastLogLevel = level;
        lastLogMsg = msg;
    });

    networkContext.connect("TestSSID", "secretPass");
    TEST_ASSERT_TRUE(lastLogLevel.length() > 0);
    TEST_ASSERT_TRUE(lastLogMsg.indexOf("TestSSID") >= 0);

    networkContext.setLogger(nullptr);
}

void test_network_context_connect_timeout_failure(void) {
    MockWiFiClass::mockConnectFails = true;
    MockWiFiClass::mockStatus = WL_DISCONNECTED;

    bool result = networkContext.connect("FailSSID", "wrongPass", "hostname", 1);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQUAL_INT(WL_DISCONNECTED, MockWiFiClass::mockStatus);
}

void test_network_context_connect_keep_ap_and_hostname_failure(void) {
    MockWiFiClass::mockSetHostnameFails = true;

    // keepAPMode = true does not switch mode to WIFI_STA before connecting
    bool result = networkContext.connect("KeepAPSSID", "pass", "new-host", 2, true);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("new-host", networkContext.getHostname().c_str());
}

void test_network_context_scan_networks_vector_done(void) {
    MockWiFiClass::mockScanResult = 3;
    std::vector<WiFiNetworkInfo> list;
    int n = networkContext.scanNetworks(list);

    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_size_t(3, list.size());
    TEST_ASSERT_EQUAL_STRING("WiFiNetwork_0", list[0].ssid.c_str());
    TEST_ASSERT_EQUAL_INT(-65, list[0].rssi);
    TEST_ASSERT_EQUAL_INT(6, list[0].channel);

    // Scan running (-1)
    MockWiFiClass::mockScanResult = WIFI_SCAN_RUNNING;
    list.clear();
    n = networkContext.scanNetworks(list);
    TEST_ASSERT_EQUAL_INT(-1, n);
    TEST_ASSERT_EQUAL_size_t(0, list.size());

    // Scan trigger (-2)
    MockWiFiClass::mockScanResult = WIFI_SCAN_FAILED;
    list.clear();
    n = networkContext.scanNetworks(list);
    TEST_ASSERT_EQUAL_INT(-2, n);
    TEST_ASSERT_EQUAL_size_t(0, list.size());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_network_context_connect_success);
    RUN_TEST(test_network_context_hostname_and_logger);
    RUN_TEST(test_network_context_connect_timeout_failure);
    RUN_TEST(test_network_context_connect_keep_ap_and_hostname_failure);
    RUN_TEST(test_network_context_disconnect);
    RUN_TEST(test_network_context_ap_mode_start_and_stop);
    RUN_TEST(test_network_context_get_ip);
    RUN_TEST(test_network_context_check_connection);
    RUN_TEST(test_network_context_scan_networks_done);
    RUN_TEST(test_network_context_scan_networks_running);
    RUN_TEST(test_network_context_scan_networks_trigger);
    RUN_TEST(test_network_context_scan_networks_vector_done);
    return UNITY_END();
}
