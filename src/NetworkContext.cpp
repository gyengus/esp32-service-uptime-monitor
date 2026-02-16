#include <WiFi.h>
#include "esp_wifi.h"
#include <esp_netif.h>
#include <Elog.h>

#include "NetworkContext.h"
#include "Config.h"

NetworkContext::NetworkContext() {  }

NetworkContext& NetworkContext::getInstance() {
    static NetworkContext instance;
    return instance;
}

bool NetworkContext::connect(const String &ssid, const String &password, bool keepAPMode) {
    Logger.info(MAINLOG, "Connecting to WiFi: %s...", ssid.c_str());
    if (!keepAPMode) {
        WiFi.mode(WIFI_STA);
    }
    if (!WiFi.setHostname(config.getConfig().hostname.c_str())) {
        Logger.error(MAINLOG, "Failed to set hostname to: %s", config.getConfig().hostname.c_str());
    }
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        const char* new_hostname = config.getConfig().hostname.c_str();
        esp_err_t err = esp_netif_set_hostname(netif, new_hostname);
        
        if (err == ESP_OK) {
            char buffer[100];
            snprintf(buffer, sizeof(buffer), "Hostname set successfully to: %s", new_hostname);
            Logger.info(MAINLOG, buffer);
        } else {
            Logger.error(MAINLOG, "Failed to set hostname, error code: %d", err);
        }
    } else {
        Logger.error(MAINLOG, "Failed to get STA interface handle!");
    }
    WiFi.begin(ssid.c_str(), password.c_str());
    uint8_t i = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        i++;
        if (i >= (WIFI_CONNECT_TIMEOUT * 10)) {
            WiFi.disconnect(false, true);
            Logger.error(MAINLOG, "Failed to connect to WiFi: %s", ssid.c_str());
            return false;
        }
    }
    WiFi.setHostname(config.getConfig().hostname.c_str());
    Logger.info(MAINLOG, "Connected to WiFi: %s, IP address: %s", ssid, WiFi.localIP().toString());
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
    Logger.info(MAINLOG, "Disconnected from WiFi");
}

void NetworkContext::checkConnection() {
    if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
        Logger.warning(MAINLOG, "WiFi disconnected, reconnecting to SSID: %s", currentSSID);
        connect(currentSSID, currentPassword);
    }
}

void NetworkContext::startAPMode() {
    Logger.info(MAINLOG, "Starting AP mode, SSID: %s, Password: %s", WIFI_AP_SSID, WIFI_AP_PASSWORD);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, 8, 0, 4);
    IPAddress IP = WiFi.softAPIP();
    Logger.info(MAINLOG, "AP IP address: %s", IP.toString());
    applyCommonWiFiSettings(true);
}

void NetworkContext::stopAPMode() {
    Logger.info(MAINLOG, "Stopping AP mode");
    WiFi.softAPdisconnect(true);
}

void NetworkContext::applyCommonWiFiSettings(bool isAP) {
    if (!WiFi.setTxPower(WIFI_POWER_19_5dBm)) {
        Logger.error(MAINLOG, "Failed to set WiFi TX power");
    }
    esp_err_t  error = esp_wifi_set_ps(WIFI_PS_NONE);
    if (error != ESP_OK) {
        Logger.error(MAINLOG, "Failed to set WiFi PS mode: %s", esp_err_to_name(error));
    }
    error = esp_wifi_set_bandwidth(isAP ? WIFI_IF_AP : WIFI_IF_STA, WIFI_BW_HT40);
    if (error != ESP_OK) {
        Logger.error(MAINLOG, "Failed to set WiFi bandwidth: %s", esp_err_to_name(error));
    }
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
    Logger.debug(MAINLOG, "Found %d networks", n);
}

NetworkContext& networkContext = NetworkContext::getInstance();
