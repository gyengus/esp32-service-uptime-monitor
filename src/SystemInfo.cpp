#include "SystemInfo.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ctime>
#include "driver/temperature_sensor.h"

#include "Config.h"
#include "MonitoringService.h"
#include "NetworkContext.h"
#include "Storage.h"
#include "WsService.h"
#include "devices.h"
#include "globals.h"
#include "version.h"

#ifndef UNIT_TEST
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace {

uint32_t getMaxFreeBlockBytes() {
#ifdef UNIT_TEST
    return 180000;
#else
    return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
#endif
}

const char* getResetReasonString() {
#ifdef UNIT_TEST
    return "POWERON";
#else
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
#endif
}

const char* getRunningOtaPartitionLabel() {
#ifdef UNIT_TEST
    return "app0";
#else
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running ? running->label : "unknown";
#endif
}

uint32_t getMonitoringStackHighWaterBytes() {
#ifdef UNIT_TEST
    return 4096;
#else
    if (!MonitoringTaskHandle) return 0;
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(MonitoringTaskHandle) * sizeof(StackType_t));
#endif
}

String formatEfuseMac() {
    uint64_t mac = ESP.getEfuseMac();
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned int>(mac & 0xFF),
             static_cast<unsigned int>((mac >> 8) & 0xFF),
             static_cast<unsigned int>((mac >> 16) & 0xFF),
             static_cast<unsigned int>((mac >> 24) & 0xFF),
             static_cast<unsigned int>((mac >> 32) & 0xFF),
             static_cast<unsigned int>((mac >> 40) & 0xFF));
    return String(buf);
}

void formatLocalTime(time_t now, String& formattedOut, bool& syncedOut) {
    syncedOut = (now > 100000);
    if (!syncedOut) {
        formattedOut = "";
        return;
    }
    struct tm tmInfo;
    localtime_r(&now, &tmInfo);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmInfo);
    formattedOut = String(buf);
}

bool readCpuTemperature(float& tempOut) {
#ifdef HAS_TEMPERATURE_SENSOR
    if (temp_sensor != NULL && temperature_sensor_get_celsius(temp_sensor, &tempOut) == ESP_OK) {
        return true;
    }
#endif
    return false;
}

String readUiVersion() {
    const String versionPath = String(WEB_UI_PATH) + "/.version";
    if (!storage.fileExists(versionPath)) {
        return "";
    }
    String ver = storage.readFile(versionPath);
    ver.trim();
    return ver;
}

} // namespace

void SystemInfo::fillBasicSysinfo(JsonObject& doc) {
    float tsensOut = 0.0f;
    if (readCpuTemperature(tsensOut)) {
        doc["temperature"] = tsensOut;
    }

    doc["heapSize"] = ESP.getHeapSize();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minFreeHeap"] = ESP.getMinFreeHeap();
    doc["maxAllocHeap"] = ESP.getMaxAllocHeap();
    doc["maxFreeBlock"] = getMaxFreeBlockBytes();
    doc["psramSize"] = ESP.getPsramSize();
    doc["freePsram"] = ESP.getFreePsram();
    doc["minFreePsram"] = ESP.getMinFreePsram();
    doc["maxAllocPsram"] = ESP.getMaxAllocPsram();
    doc["rssi"] = WiFi.RSSI();
    doc["wsClients"] = wsService.count();
    doc["uptime"] = millis() / 1000;

    String localTimeStr;
    bool timeSynced = false;
    formatLocalTime(time(nullptr), localTimeStr, timeSynced);
    doc["localTime"] = localTimeStr;
    doc["timeSynced"] = timeSynced;

    doc["lastCheckDurationMs"] = monitoringService.getLastCheckDurationMs();
    String lastCheckStr;
    bool lastCheckValid = false;
    formatLocalTime(monitoringService.getLastCheckTime(), lastCheckStr, lastCheckValid);
    doc["lastCheckTime"] = lastCheckValid ? lastCheckStr : String("");
    doc["stackHighWaterBytes"] = getMonitoringStackHighWaterBytes();

    doc["version"] = FIRMWARE_VERSION;
    doc["buildDate"] = BUILD_TIMESTAMP;
    doc["commit"] = COMMIT_HASH;
    doc["configEnabled"] = isConfigFunctionsEnabled;
    doc["configTimeoutRemaining"] = getConfigModeRemainingSeconds();
}

void SystemInfo::fillDetailedSysinfo(JsonObject& doc) {
    fillBasicSysinfo(doc);

    const config_t cfg = config.getConfig();

    float tsensOut = 0.0f;
    bool hasTemp = readCpuTemperature(tsensOut);

    JsonObject device = doc["device"].to<JsonObject>();
    device["board"] = BOARD_NAME;
    device["chipModel"] = ESP.getChipModel();
    device["chipRevision"] = ESP.getChipRevision();
    device["chipCores"] = ESP.getChipCores();
    device["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    device["resetReason"] = getResetReasonString();
    device["efuseMac"] = formatEfuseMac();
    device["hasTempSensor"] = hasTemp;

    JsonObject memory = doc["memory"].to<JsonObject>();
    memory["heapSize"] = ESP.getHeapSize();
    memory["freeHeap"] = ESP.getFreeHeap();
    memory["minFreeHeap"] = ESP.getMinFreeHeap();
    memory["maxAllocHeap"] = ESP.getMaxAllocHeap();
    memory["maxFreeBlock"] = getMaxFreeBlockBytes();
    memory["psramSize"] = ESP.getPsramSize();
    memory["freePsram"] = ESP.getFreePsram();
    memory["minFreePsram"] = ESP.getMinFreePsram();
    memory["maxAllocPsram"] = ESP.getMaxAllocPsram();
    memory["flashSize"] = ESP.getFlashChipSize();
    memory["flashSpeed"] = ESP.getFlashChipSpeed();
    memory["sketchSize"] = ESP.getSketchSize();
    memory["freeSketchSpace"] = ESP.getFreeSketchSpace();

    StorageStats st = storage.getStats();
    JsonObject storageObj = doc["storage"].to<JsonObject>();
    storageObj["backend"] = st.backend;
    storageObj["available"] = st.available;
    storageObj["cardType"] = st.cardType;
    storageObj["cardSizeBytes"] = st.cardSizeBytes;
    storageObj["totalBytes"] = st.totalBytes;
    storageObj["usedBytes"] = st.usedBytes;
    storageObj["freeBytes"] = st.freeBytes;

    JsonObject network = doc["network"].to<JsonObject>();
    network["mode"] = isAPMode ? "AP" : "STA";
    network["connected"] = isAPMode ? true : networkContext.isConnected();
    network["ssid"] = isAPMode ? String(WIFI_AP_SSID) : WiFi.SSID();
    network["bssid"] = isAPMode ? String("") : WiFi.BSSIDstr();
    network["channel"] = WiFi.channel();
    network["rssi"] = WiFi.RSSI();
    network["ip"] = isAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    network["subnetMask"] = isAPMode ? String("") : WiFi.subnetMask().toString();
    network["gateway"] = isAPMode ? String("") : WiFi.gatewayIP().toString();
    network["dns"] = isAPMode ? String("") : WiFi.dnsIP().toString();
    network["mac"] = WiFi.macAddress();
    network["hostname"] = cfg.hostname;
    network["wsClients"] = wsService.count();

    String localTimeStr;
    bool timeSynced = false;
    formatLocalTime(time(nullptr), localTimeStr, timeSynced);
    JsonObject timeObj = doc["time"].to<JsonObject>();
    timeObj["localTime"] = localTimeStr;
    timeObj["timeSynced"] = timeSynced;
    timeObj["sntpServer"] = cfg.sntpServer;
    timeObj["timezoneOffset"] = cfg.offsetHours;
    timeObj["uptime"] = millis() / 1000;

    JsonObject monitoring = doc["monitoring"].to<JsonObject>();
    monitoring["checkInterval"] = cfg.checkInterval;
    monitoring["lastCheckDurationMs"] = monitoringService.getLastCheckDurationMs();
    time_t lastCheck = monitoringService.getLastCheckTime();
    String lastCheckStr;
    bool lastCheckValid = false;
    formatLocalTime(lastCheck, lastCheckStr, lastCheckValid);
    monitoring["lastCheckTime"] = lastCheckValid ? lastCheckStr : String("");
    monitoring["stackHighWaterBytes"] = getMonitoringStackHighWaterBytes();

    JsonObject firmware = doc["firmware"].to<JsonObject>();
    firmware["version"] = FIRMWARE_VERSION;
    firmware["commit"] = COMMIT_HASH;
    firmware["buildDate"] = BUILD_TIMESTAMP;
    firmware["uiVersion"] = readUiVersion();
    firmware["expectedUiVersion"] = String(FIRMWARE_VERSION) + "-" + String(COMMIT_HASH);
    firmware["otaPartition"] = getRunningOtaPartitionLabel();
    firmware["sdkVersion"] = ESP.getSdkVersion();
#ifdef ESP_ARDUINO_VERSION_STR
    firmware["coreVersion"] = ESP_ARDUINO_VERSION_STR;
#else
    firmware["coreVersion"] = "";
#endif
    firmware["configEnabled"] = isConfigFunctionsEnabled;
}
