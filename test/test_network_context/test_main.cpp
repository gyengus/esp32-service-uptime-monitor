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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_network_context_connect_success);
    RUN_TEST(test_network_context_disconnect);
    RUN_TEST(test_network_context_ap_mode_start_and_stop);
    RUN_TEST(test_network_context_get_ip);
    RUN_TEST(test_network_context_check_connection);
    RUN_TEST(test_network_context_scan_networks_done);
    RUN_TEST(test_network_context_scan_networks_running);
    RUN_TEST(test_network_context_scan_networks_trigger);
    return UNITY_END();
}
