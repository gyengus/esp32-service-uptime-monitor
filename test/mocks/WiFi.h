#ifndef MOCK_WIFI_H
#define MOCK_WIFI_H
#include <Arduino.h>
#include <cstdio>
#include "esp_wifi.h"

class MockIPAddress {
private:
    uint8_t _bytes[4];
public:
    MockIPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0) {
        _bytes[0] = a; _bytes[1] = b; _bytes[2] = c; _bytes[3] = d;
    }
    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", _bytes[0], _bytes[1], _bytes[2], _bytes[3]);
        return String(buf);
    }
    bool operator==(const MockIPAddress& other) const {
        return _bytes[0] == other._bytes[0] && _bytes[1] == other._bytes[1] &&
               _bytes[2] == other._bytes[2] && _bytes[3] == other._bytes[3];
    }
};

enum wl_status_t {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
};

enum wifi_mode_t {
    WIFI_OFF = 0,
    WIFI_STA = 1,
    WIFI_AP = 2,
    WIFI_AP_STA = 3
};

#define WIFI_SCAN_RUNNING -1
#define WIFI_SCAN_FAILED -2

class MockWiFiClass {
public:
    static wl_status_t mockStatus;
    static wifi_mode_t mockMode;
    static MockIPAddress mockIP;
    static int mockScanResult;
    static String lastSSID;
    static String lastPassword;
    static String lastHostname;

    static void resetMock() {
        mockStatus = WL_DISCONNECTED;
        mockMode = WIFI_OFF;
        mockIP = MockIPAddress(192, 168, 1, 105);
        mockScanResult = 0;
        lastSSID = "";
        lastPassword = "";
        lastHostname = "";
    }

    void mode(wifi_mode_t m) { mockMode = m; }
    wifi_mode_t getMode() { return mockMode; }

    bool setHostname(const char* name) {
        lastHostname = name;
        return true;
    }

    wl_status_t begin(const char* ssid, const char* passphrase = nullptr) {
        lastSSID = ssid;
        lastPassword = passphrase ? passphrase : "";
        mockMode = WIFI_STA;
        mockStatus = WL_CONNECTED;
        return mockStatus;
    }

    wl_status_t status() { return mockStatus; }
    void disconnect(bool wifioff = false, bool eraseap = false) {
        mockStatus = WL_DISCONNECTED;
        if (wifioff) mockMode = WIFI_OFF;
    }

    MockIPAddress localIP() { return mockIP; }
    MockIPAddress softAPIP() { return MockIPAddress(192, 168, 4, 1); }

    bool softAP(const char*, const char* = nullptr, int = 1, int = 0, int = 4) {
        mockMode = WIFI_AP;
        return true;
    }
    bool softAPdisconnect(bool) { return true; }

    bool setTxPower(wifi_power_t) { return true; }

    int8_t scanComplete() { return (int8_t)mockScanResult; }
    int scanNetworks(bool = false, bool = false) { return mockScanResult; }
    void scanDelete() {}

    String SSID(int i) { return "WiFiNetwork_" + String(i); }
    int32_t RSSI(int = 0) { return -65; }
    String BSSIDstr(int i) { return "AA:BB:CC:DD:EE:0" + String(i); }
    int32_t channel(int) { return 6; }
    uint8_t encryptionType(int) { return 3; }
};

inline wl_status_t MockWiFiClass::mockStatus = WL_CONNECTED;
inline wifi_mode_t MockWiFiClass::mockMode = WIFI_OFF;
inline MockIPAddress MockWiFiClass::mockIP = MockIPAddress(192, 168, 1, 105);
inline int MockWiFiClass::mockScanResult = 0;
inline String MockWiFiClass::lastSSID = "";
inline String MockWiFiClass::lastPassword = "";
inline String MockWiFiClass::lastHostname = "";

inline MockWiFiClass WiFi;

#endif // MOCK_WIFI_H
