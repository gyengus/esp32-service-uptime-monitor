#ifndef _NETWORK_CONTEXT_H_
#define _NETWORK_CONTEXT_H_

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class NetworkContext {
public:
    NetworkContext(const NetworkContext&) = delete;
    NetworkContext& operator=(const NetworkContext&) = delete;
    static NetworkContext& getInstance();

    bool connect(const String &ssid, const String &password, bool keepAPMode = false);
    bool isConnected();
    void disconnect();
    void checkConnection();
    void startAPMode();
    void stopAPMode();
    String getIP();
    void scanNetworks(JsonDocument& target);
private:
    NetworkContext();
    void applyCommonWiFiSettings(bool isAP);
    String currentSSID;
    String currentPassword;
};

extern NetworkContext& networkContext;

#endif // _NETWORK_CONTEXT_H_
