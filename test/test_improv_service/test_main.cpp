#include <unity.h>
#include <Arduino.h>
#include <ArduinoFake.h>
#include <vector>
#include <cstring>

#include "ImprovService.h"
#include "Esp.h"
#include "Config.h"
#include "Storage.h"
#include "NetworkContext.h"
#include "LedService.h"
#include "WiFi.h"
#include "version.h"
#include "devices.h"

using namespace fakeit;

#include "../mocks/MockGlobals.cpp"

class MockImprovStream : public Stream {
public:
    std::vector<uint8_t> rxBuffer;
    size_t rxPos = 0;
    std::vector<uint8_t> txBuffer;
    bool flushed = false;

    void clear() {
        rxBuffer.clear();
        rxPos = 0;
        txBuffer.clear();
        flushed = false;
    }

    void pushRxByte(uint8_t b) {
        rxBuffer.push_back(b);
    }

    void pushRxPacket(uint8_t type, const std::vector<uint8_t>& payload, bool corruptChecksum = false) {
        std::vector<uint8_t> pkt = {'I', 'M', 'P', 'R', 'O', 'V', 0x01, type, static_cast<uint8_t>(payload.size())};
        pkt.insert(pkt.end(), payload.begin(), payload.end());
        uint8_t checksum = 0;
        for (uint8_t b : pkt) {
            checksum += b;
        }
        if (corruptChecksum) {
            checksum ^= 0xFF;
        }
        pkt.push_back(checksum);
        for (uint8_t b : pkt) {
            rxBuffer.push_back(b);
        }
    }

    void pushRpcCommand(uint8_t cmd, const std::vector<uint8_t>& cmdData = {}, bool corruptChecksum = false) {
        std::vector<uint8_t> payload;
        payload.push_back(cmd);
        payload.push_back(static_cast<uint8_t>(cmdData.size()));
        payload.insert(payload.end(), cmdData.begin(), cmdData.end());
        pushRxPacket(improv::TYPE_RPC, payload, corruptChecksum);
    }

    int available() override {
        return static_cast<int>(rxBuffer.size() - rxPos);
    }

    int read() override {
        if (rxPos >= rxBuffer.size()) {
            return -1;
        }
        return rxBuffer[rxPos++];
    }

    int peek() override {
        if (rxPos >= rxBuffer.size()) {
            return -1;
        }
        return rxBuffer[rxPos];
    }

    void flush() override {
        flushed = true;
    }

    size_t write(uint8_t b) override {
        txBuffer.push_back(b);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        txBuffer.insert(txBuffer.end(), buffer, buffer + size);
        return size;
    }
};

struct ParsedImprovPacket {
    uint8_t type;
    std::vector<uint8_t> data;
    bool checksumValid;
};

static std::vector<ParsedImprovPacket> parseTxPackets(const std::vector<uint8_t>& tx) {
    std::vector<ParsedImprovPacket> result;
    size_t i = 0;
    while (i + 10 <= tx.size()) {
        if (tx[i] == 'I' && tx[i + 1] == 'M' && tx[i + 2] == 'P' &&
            tx[i + 3] == 'R' && tx[i + 4] == 'O' && tx[i + 5] == 'V' &&
            tx[i + 6] == 0x01) {
            uint8_t type = tx[i + 7];
            uint8_t len = tx[i + 8];
            if (i + 9 + len < tx.size()) {
                uint8_t expectedSum = 0;
                for (size_t j = 0; j < 9 + static_cast<size_t>(len); j++) {
                    expectedSum += tx[i + j];
                }
                uint8_t actualSum = tx[i + 9 + len];
                ParsedImprovPacket pkt;
                pkt.type = type;
                pkt.data.assign(tx.begin() + i + 9, tx.begin() + i + 9 + len);
                pkt.checksumValid = (expectedSum == actualSum);
                result.push_back(pkt);
                i += 10 + len;
                if (i < tx.size() && tx[i] == '\n') {
                    i++;
                }
                continue;
            }
        }
        i++;
    }
    return result;
}

static std::vector<String> parseRpcStrings(const std::vector<uint8_t>& data) {
    std::vector<String> strings;
    if (data.size() < 2) {
        return strings;
    }
    size_t offset = 2;
    size_t end = 2 + data[1];
    if (end > data.size()) {
        end = data.size();
    }
    while (offset < end) {
        uint8_t len = data[offset++];
        String s;
        for (uint8_t i = 0; i < len && offset < end; i++) {
            s += static_cast<char>(data[offset++]);
        }
        strings.push_back(s);
    }
    return strings;
}

static MockImprovStream mockStream;

void setUp(void) {
    ArduinoFakeReset();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), pinMode)).AlwaysReturn();
    When(Method(ArduinoFake(), digitalWrite)).AlwaysReturn();

    storage.clear();
    config_t defaultCfg;
    config.saveConfig(defaultCfg);
    storage.clear();
    MockWiFiClass::resetMock();
    improvService.resetParser();
    mockStream.clear();

    isConfigFunctionsEnabled = false;
    isAPMode = true;
    shouldRestart = false;
    isOtaUpdating = false;
}

void tearDown(void) {
    ArduinoFakeReset();
    storage.clear();
    config_t defaultCfg;
    config.saveConfig(defaultCfg);
    storage.clear();
    MockWiFiClass::resetMock();
    improvService.resetParser();
    mockStream.clear();
}

void test_improv_get_current_state_unprovisioned(void) {
    isAPMode = true;
    MockWiFiClass::mockStatus = WL_DISCONNECTED;

    // 0x02 is GET_CURRENT_STATE in Improv Serial v1
    TEST_ASSERT_EQUAL_UINT8(0x02, improv::CMD_GET_CURRENT_STATE);
    mockStream.pushRpcCommand(improv::CMD_GET_CURRENT_STATE);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(2, packets.size());
    TEST_ASSERT_TRUE(packets[0].checksumValid);
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[0].type);
    TEST_ASSERT_EQUAL_UINT8(improv::ERROR_NONE, packets[0].data[0]);

    TEST_ASSERT_TRUE(packets[1].checksumValid);
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_CURRENT_STATE, packets[1].type);
    TEST_ASSERT_EQUAL_UINT8(improv::STATE_AUTHORIZED, packets[1].data[0]);
}

void test_improv_get_current_state_already_provisioned(void) {
    isAPMode = false;
    MockWiFiClass::mockStatus = WL_CONNECTED;
    MockWiFiClass::mockMode = WIFI_STA;
    MockWiFiClass::mockIP = MockIPAddress(192, 168, 1, 42);

    mockStream.pushRpcCommand(improv::CMD_GET_CURRENT_STATE);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(3, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[0].type);
    TEST_ASSERT_EQUAL_UINT8(improv::ERROR_NONE, packets[0].data[0]);

    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_CURRENT_STATE, packets[1].type);
    TEST_ASSERT_EQUAL_UINT8(improv::STATE_PROVISIONED, packets[1].data[0]);

    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_RPC_RESPONSE, packets[2].type);
    TEST_ASSERT_EQUAL_UINT8(improv::CMD_GET_CURRENT_STATE, packets[2].data[0]);
    auto urls = parseRpcStrings(packets[2].data);
    TEST_ASSERT_EQUAL_size_t(1, urls.size());
    TEST_ASSERT_EQUAL_STRING("http://esp32-service-uptime-monitor.local/", urls[0].c_str());
}

void test_improv_get_device_info(void) {
    config_t cfg;
    cfg.hostname = "my-uptime-node";
    config.saveConfig(cfg);

    // 0x03 is GET_DEVICE_INFO in Improv Serial v1
    TEST_ASSERT_EQUAL_UINT8(0x03, improv::CMD_GET_DEVICE_INFO);
    mockStream.pushRpcCommand(improv::CMD_GET_DEVICE_INFO);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(2, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_RPC_RESPONSE, packets[1].type);
    TEST_ASSERT_EQUAL_UINT8(improv::CMD_GET_DEVICE_INFO, packets[1].data[0]);

    auto info = parseRpcStrings(packets[1].data);
    TEST_ASSERT_EQUAL_size_t(4, info.size());
    TEST_ASSERT_EQUAL_STRING("ESP32 Service Uptime Monitor", info[0].c_str());
    TEST_ASSERT_EQUAL_STRING(FIRMWARE_VERSION, info[1].c_str());
    TEST_ASSERT_EQUAL_STRING(ESP.getChipModel(), info[2].c_str());
    TEST_ASSERT_EQUAL_STRING("my-uptime-node", info[3].c_str());
}

void test_improv_get_wifi_networks(void) {
    MockWiFiClass::mockScanResult = 2;

    // 0x04 is GET_WIFI_NETWORKS in Improv Serial v1
    TEST_ASSERT_EQUAL_UINT8(0x04, improv::CMD_GET_WIFI_NETWORKS);
    mockStream.pushRpcCommand(improv::CMD_GET_WIFI_NETWORKS);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    // 1 ERROR_NONE + 2 network RPC responses + 1 empty terminator RPC response = 4 packets
    TEST_ASSERT_EQUAL_size_t(4, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[0].type);

    auto net0 = parseRpcStrings(packets[1].data);
    TEST_ASSERT_EQUAL_size_t(3, net0.size());
    TEST_ASSERT_EQUAL_STRING("WiFiNetwork_0", net0[0].c_str());
    TEST_ASSERT_EQUAL_STRING("-65", net0[1].c_str());
    TEST_ASSERT_EQUAL_STRING("YES", net0[2].c_str());

    auto net1 = parseRpcStrings(packets[2].data);
    TEST_ASSERT_EQUAL_size_t(3, net1.size());
    TEST_ASSERT_EQUAL_STRING("WiFiNetwork_1", net1[0].c_str());

    auto terminator = parseRpcStrings(packets[3].data);
    TEST_ASSERT_EQUAL_size_t(0, terminator.size());
}

void test_improv_hostname_and_network_state(void) {
    // 0x05 HOSTNAME Set
    const char* newHost = "Lab_Monitor_01";
    std::vector<uint8_t> hostData(newHost, newHost + strlen(newHost));
    mockStream.pushRpcCommand(improv::CMD_HOSTNAME, hostData);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(2, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_RPC_RESPONSE, packets[1].type);
    auto hostRes = parseRpcStrings(packets[1].data);
    TEST_ASSERT_EQUAL_size_t(1, hostRes.size());
    TEST_ASSERT_EQUAL_STRING("lab-monitor-01", hostRes[0].c_str());

    // 0x07 GET_NETWORK_STATE when online
    mockStream.clear();
    isAPMode = false;
    MockWiFiClass::mockStatus = WL_CONNECTED;
    MockWiFiClass::mockIP = MockIPAddress(192, 168, 0, 118);
    mockStream.pushRpcCommand(improv::CMD_GET_NETWORK_STATE);
    improvService.handleStream(mockStream);

    packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(2, packets.size());
    auto netState = parseRpcStrings(packets[1].data);
    TEST_ASSERT_EQUAL_size_t(2, netState.size());
    TEST_ASSERT_EQUAL_STRING("3", netState[0].c_str());
    TEST_ASSERT_EQUAL_STRING("http://lab-monitor-01.local/", netState[1].c_str());
}

void test_improv_wifi_settings_success(void) {
    isAPMode = true;
    MockWiFiClass::mockConnectFails = false;
    MockWiFiClass::mockIP = MockIPAddress(192, 168, 1, 150);

    const char* ssid = "HomeNetwork";
    const char* pass = "SuperSecret123";
    std::vector<uint8_t> cmdData;
    cmdData.push_back(static_cast<uint8_t>(strlen(ssid)));
    cmdData.insert(cmdData.end(), ssid, ssid + strlen(ssid));
    cmdData.push_back(static_cast<uint8_t>(strlen(pass)));
    cmdData.insert(cmdData.end(), pass, pass + strlen(pass));

    mockStream.pushRpcCommand(improv::CMD_WIFI_SETTINGS, cmdData);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    // Expected: ERROR_NONE, STATE_PROVISIONING, STATE_PROVISIONED, RPC_RESPONSE(URL)
    TEST_ASSERT_EQUAL_size_t(4, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[0].type);
    TEST_ASSERT_EQUAL_UINT8(improv::ERROR_NONE, packets[0].data[0]);

    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_CURRENT_STATE, packets[1].type);
    TEST_ASSERT_EQUAL_UINT8(improv::STATE_PROVISIONING, packets[1].data[0]);

    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_CURRENT_STATE, packets[2].type);
    TEST_ASSERT_EQUAL_UINT8(improv::STATE_PROVISIONED, packets[2].data[0]);

    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_RPC_RESPONSE, packets[3].type);
    TEST_ASSERT_EQUAL_UINT8(improv::CMD_WIFI_SETTINGS, packets[3].data[0]);
    auto urls = parseRpcStrings(packets[3].data);
    TEST_ASSERT_EQUAL_size_t(1, urls.size());
    TEST_ASSERT_EQUAL_STRING("http://esp32-service-uptime-monitor.local/", urls[0].c_str());

    TEST_ASSERT_TRUE(mockStream.flushed);
    TEST_ASSERT_TRUE(shouldRestart);
    TEST_ASSERT_TRUE(storage.fileExists(CONFIG_FILE_PATH));

    config.loadConfig();
    TEST_ASSERT_EQUAL_STRING("HomeNetwork", config.getConfig().wifiSSID.c_str());
    TEST_ASSERT_EQUAL_STRING("SuperSecret123", config.getConfig().wifiPassword.c_str());
}

void test_improv_wifi_settings_connection_failure(void) {
    isAPMode = true;
    MockWiFiClass::mockConnectFails = true;

    const char* ssid = "BadNetwork";
    const char* pass = "WrongPass";
    std::vector<uint8_t> cmdData;
    cmdData.push_back(static_cast<uint8_t>(strlen(ssid)));
    cmdData.insert(cmdData.end(), ssid, ssid + strlen(ssid));
    cmdData.push_back(static_cast<uint8_t>(strlen(pass)));
    cmdData.insert(cmdData.end(), pass, pass + strlen(pass));

    mockStream.pushRpcCommand(improv::CMD_WIFI_SETTINGS, cmdData);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    // Expected: ERROR_NONE, STATE_PROVISIONING, ERROR_UNABLE_TO_CONNECT, STATE_AUTHORIZED
    TEST_ASSERT_EQUAL_size_t(4, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::STATE_PROVISIONING, packets[1].data[0]);
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[2].type);
    TEST_ASSERT_EQUAL_UINT8(improv::ERROR_UNABLE_TO_CONNECT, packets[2].data[0]);
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_CURRENT_STATE, packets[3].type);
    TEST_ASSERT_EQUAL_UINT8(improv::STATE_AUTHORIZED, packets[3].data[0]);

    TEST_ASSERT_FALSE(shouldRestart);
    TEST_ASSERT_FALSE(storage.fileExists(CONFIG_FILE_PATH));
}

void test_improv_error_handling_and_resync(void) {
    // 1. Corrupt checksum triggers ERROR_INVALID_RPC
    mockStream.pushRpcCommand(improv::CMD_GET_CURRENT_STATE, {}, true);
    improvService.handleStream(mockStream);

    auto packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(1, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[0].type);
    TEST_ASSERT_EQUAL_UINT8(improv::ERROR_INVALID_RPC, packets[0].data[0]);

    // 2. Unknown RPC command triggers ERROR_UNKNOWN_RPC
    mockStream.clear();
    mockStream.pushRpcCommand(0x99);
    improvService.handleStream(mockStream);

    packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(1, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_ERROR_STATE, packets[0].type);
    TEST_ASSERT_EQUAL_UINT8(improv::ERROR_UNKNOWN_RPC, packets[0].data[0]);

    // 3. Partial header or serial noise followed by valid command recovers cleanly
    mockStream.clear();
    mockStream.pushRxByte('I');
    mockStream.pushRxByte('M');
    mockStream.pushRpcCommand(improv::CMD_GET_CURRENT_STATE);
    improvService.handleStream(mockStream);

    packets = parseTxPackets(mockStream.txBuffer);
    TEST_ASSERT_EQUAL_size_t(2, packets.size());
    TEST_ASSERT_EQUAL_UINT8(improv::TYPE_CURRENT_STATE, packets[1].type);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_improv_get_current_state_unprovisioned);
    RUN_TEST(test_improv_get_current_state_already_provisioned);
    RUN_TEST(test_improv_get_device_info);
    RUN_TEST(test_improv_get_wifi_networks);
    RUN_TEST(test_improv_hostname_and_network_state);
    RUN_TEST(test_improv_wifi_settings_success);
    RUN_TEST(test_improv_wifi_settings_connection_failure);
    RUN_TEST(test_improv_error_handling_and_resync);
    return UNITY_END();
}
