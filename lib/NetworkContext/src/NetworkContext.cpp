#include "NetworkContext.h"
#include <WiFi.h>

#ifndef UNIT_TEST
#include "esp_wifi.h"
#include <esp_netif.h>
#endif

NetworkContext::NetworkContext() : connectTimeoutSeconds(10), logHandler(nullptr) { }

NetworkContext& NetworkContext::getInstance() {
    static NetworkContext instance;
    return instance;
}

void NetworkContext::setLogger(LogHandler handler) {
    logHandler = handler;
}

void NetworkContext::log(const char* level, const char* msg) {
    if (logHandler) {
        logHandler(level, msg);
    } else {
#ifndef UNIT_TEST
        if (strcmp(level, "ERROR") == 0) {
            log_e("%s", msg);
        } else if (strcmp(level, "WARNING") == 0) {
            log_w("%s", msg);
        } else if (strcmp(level, "DEBUG") == 0) {
            log_d("%s", msg);
        } else {
            log_i("%s", msg);
        }
#endif
    }
}

void NetworkContext::setHostname(const String& hostname) {
    currentHostname = hostname;
}

String NetworkContext::getHostname() const {
    return currentHostname;
}

bool NetworkContext::connect(const String &ssid, const String &password, const String &hostname, uint32_t timeoutSeconds, bool keepAPMode) {
    char logBuf[128];
    snprintf(logBuf, sizeof(logBuf), "Connecting to WiFi: %s...", ssid.c_str());
    log("INFO", logBuf);

    if (hostname.length() > 0) {
        currentHostname = hostname;
    }
    if (timeoutSeconds > 0) {
        connectTimeoutSeconds = timeoutSeconds;
    }

    if (!keepAPMode) {
        WiFi.mode(WIFI_STA);
    }

    if (currentHostname.length() > 0) {
        if (!WiFi.setHostname(currentHostname.c_str())) {
            snprintf(logBuf, sizeof(logBuf), "Failed to set hostname to: %s", currentHostname.c_str());
            log("ERROR", logBuf);
        }
#ifndef UNIT_TEST
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL) {
            esp_err_t err = esp_netif_set_hostname(netif, currentHostname.c_str());
            if (err == ESP_OK) {
                snprintf(logBuf, sizeof(logBuf), "Hostname set successfully to: %s", currentHostname.c_str());
                log("INFO", logBuf);
            } else {
                snprintf(logBuf, sizeof(logBuf), "Failed to set hostname, error code: %d", err);
                log("ERROR", logBuf);
            }
        } else {
            log("ERROR", "Failed to get STA interface handle!");
        }
#endif
    }

    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t i = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        i++;
        if (i >= (connectTimeoutSeconds * 10)) {
            WiFi.disconnect(false, true);
            snprintf(logBuf, sizeof(logBuf), "Failed to connect to WiFi: %s", ssid.c_str());
            log("ERROR", logBuf);
            return false;
        }
    }

    if (currentHostname.length() > 0) {
        WiFi.setHostname(currentHostname.c_str());
    }

    snprintf(logBuf, sizeof(logBuf), "Connected to WiFi: %s, IP address: %s", ssid.c_str(), WiFi.localIP().toString().c_str());
    log("INFO", logBuf);

    applyCommonWiFiSettings(false);
    currentSSID = ssid;
    currentPassword = password;
    return true;
}

bool NetworkContext::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void NetworkContext::disconnect() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    log("INFO", "Disconnected from WiFi");
}

void NetworkContext::checkConnection() {
    if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
        char logBuf[128];
        snprintf(logBuf, sizeof(logBuf), "WiFi disconnected, reconnecting to SSID: %s", currentSSID.c_str());
        log("WARNING", logBuf);
        connect(currentSSID, currentPassword, currentHostname, connectTimeoutSeconds);
    }
}

void NetworkContext::startAPMode(const String &ssid, const String &password, uint8_t channel, uint8_t maxConnections) {
    char logBuf[128];
    snprintf(logBuf, sizeof(logBuf), "Starting AP mode, SSID: %s, Password: %s", ssid.c_str(), password.c_str());
    log("INFO", logBuf);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr, channel, 0, maxConnections);
    auto IP = WiFi.softAPIP();

    snprintf(logBuf, sizeof(logBuf), "AP IP address: %s", IP.toString().c_str());
    log("INFO", logBuf);

    applyCommonWiFiSettings(true);
}

void NetworkContext::stopAPMode() {
    log("INFO", "Stopping AP mode");
    WiFi.softAPdisconnect(true);
}

void NetworkContext::applyCommonWiFiSettings(bool isAP) {
#ifndef UNIT_TEST
    if (!WiFi.setTxPower(WIFI_POWER_19_5dBm)) {
        log("ERROR", "Failed to set WiFi TX power");
    }
    esp_err_t error = esp_wifi_set_ps(WIFI_PS_NONE);
    if (error != ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Failed to set WiFi PS mode: %s", esp_err_to_name(error));
        log("ERROR", buf);
    }
    error = esp_wifi_set_bandwidth(isAP ? WIFI_IF_AP : WIFI_IF_STA, WIFI_BW_HT40);
    if (error != ESP_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Failed to set WiFi bandwidth: %s", esp_err_to_name(error));
        log("ERROR", buf);
    }
#endif
}

String NetworkContext::getIP() {
    if (!isConnected()) {
        return String();
    }
    if (WiFi.getMode() == WIFI_AP) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

int NetworkContext::scanNetworks(std::vector<WiFiNetworkInfo>& results) {
    results.clear();
    int8_t n = WiFi.scanComplete();

    if (n == -2) {
        WiFi.scanNetworks(true, false);
        return -2; // scan initiated
    } else if (n == -1) {
        return -1; // scan in progress
    }

    for (uint8_t i = 0; i < n; ++i) {
        WiFiNetworkInfo info;
        info.ssid = WiFi.SSID(i);
        info.rssi = WiFi.RSSI(i);
        info.bssid = WiFi.BSSIDstr(i);
        info.channel = WiFi.channel(i);
        info.encryptionType = WiFi.encryptionType(i);
        results.push_back(info);
    }
    WiFi.scanDelete();
    return n;
}

void NetworkContext::scanNetworks(JsonDocument& target) {
    int8_t n = WiFi.scanComplete();

    if (n == -2) {
        WiFi.scanNetworks(true, false);
        target["status"] = "scanning";
        return;
    } else if (n == -1) {
        target["status"] = "in progress";
        return;
    }

    target["status"] = "done";
    JsonArray networks = target["networks"].to<JsonArray>();
    for (uint8_t i = 0; i < n; ++i) {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["bssid"] = WiFi.BSSIDstr(i);
        network["channel"] = WiFi.channel(i);
        network["encryptionType"] = WiFi.encryptionType(i);
    }
    WiFi.scanDelete();

    char logBuf[64];
    snprintf(logBuf, sizeof(logBuf), "Found %d networks", n);
    log("DEBUG", logBuf);
}

NetworkContext& networkContext = NetworkContext::getInstance();
