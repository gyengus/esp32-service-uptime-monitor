#ifndef _NETWORK_CONTEXT_H_
#define _NETWORK_CONTEXT_H_

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

struct WiFiNetworkInfo {
    String ssid;
    int32_t rssi;
    String bssid;
    int32_t channel;
    uint8_t encryptionType;
};

class NetworkContext {
public:
    using LogHandler = std::function<void(const char* level, const char* msg)>;

    NetworkContext(const NetworkContext&) = delete;
    NetworkContext& operator=(const NetworkContext&) = delete;
    static NetworkContext& getInstance();

    void setLogger(LogHandler handler);
    void setHostname(const String& hostname);
    String getHostname() const;

    bool connect(const String &ssid, const String &password, const String &hostname = "", uint32_t timeoutSeconds = 10, bool keepAPMode = false);
    bool isConnected();
    void disconnect();
    void checkConnection();

    void startAPMode(const String &ssid = "ESP32-AP", const String &password = "", uint8_t channel = 1, uint8_t maxConnections = 4);
    void stopAPMode();

    String getIP();
    int scanNetworks(std::vector<WiFiNetworkInfo>& results);
    void scanNetworks(JsonDocument& target);

private:
    NetworkContext();
    void log(const char* level, const char* msg);
    void applyCommonWiFiSettings(bool isAP);

    String currentSSID;
    String currentPassword;
    String currentHostname;
    uint32_t connectTimeoutSeconds;
    LogHandler logHandler;
};

extern NetworkContext& networkContext;

#endif // _NETWORK_CONTEXT_H_
