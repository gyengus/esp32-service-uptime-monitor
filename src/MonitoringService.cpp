#include "MonitoringService.h"
#include <Elog.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "Config.h"
#include "MonitoredEntityRepository.h"
#include "NotificationService.h"
#include "WsService.h"
#include "globals.h"

MonitoringService& MonitoringService::getInstance() {
    static MonitoringService instance;
    return instance;
}

MonitoringService::MonitoringService() {
    _offline = 0;
    _online = 0;
    _entities = 0;
}

void MonitoringService::init() {
    Logger.debug(MAINLOG, "Initializing monitoring service...");
    monitoredEntityRepository.load();
    _entities = monitoredEntityRepository.size();
}

String MonitoringService::summary() {
    JsonDocument doc;
    doc["total"] = monitoredEntityRepository.size();
    doc["online"] = _online;
    doc["offline"] = _offline;
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

bool MonitoringService::checkAll() {
    auto entities = monitoredEntityRepository.getEntitiesSnapshot();
    bool mustSave = false;

    for (const auto& entitySnapshot : entities) {
        if (entitySnapshot.isDisabled()) continue;

        Logger.debug(MAINLOG, "Checking entity: %s (%s)", entitySnapshot.getName().c_str(), entitySnapshot.getAddress().c_str());
        bool currentMeasurement = check(entitySnapshot);

        bool statusChanged = false;
        bool newStatus = false;
        bool notify = false;

        if (monitoredEntityRepository.updateCheckResult(entitySnapshot.getName(), currentMeasurement, statusChanged, newStatus, notify)) {
            mustSave = true;
            if (notify) {
                _handleStatusChange(entitySnapshot.getName(), newStatus);
            }
            if (statusChanged) {
                MonitoredEntity updated;
                if (monitoredEntityRepository.getEntity(entitySnapshot.getName(), updated)) {
                    wsService.queueEntityUpdate(updated);
                }
            }
        }
    }

    if (mustSave) {
        monitoredEntityRepository.save();
    }

    unsigned int total = 0;
    monitoredEntityRepository.getCounts(_online, _offline, total);
    _entities = total;
    wsService.queueSummary(_online, _offline, total);

    return true;
}

bool MonitoringService::check(const MonitoredEntity& entity) {
    return _doHead(entity.getAddress());
}

bool MonitoringService::_doHead(const String& address) {
    bool success = false;

    HTTPClient http;
    if (address.startsWith("https")) {
        WiFiClientSecure client;
        client.setInsecure();
        http.begin(client, address);
    } else {
        http.begin(address);
    }
    http.setTimeout(HTTP_REQUEST_TIMEOUT);
    http.setReuse(false);
    int httpCode = http.sendRequest("HEAD"); 
    if (httpCode > 0) {
        success = true;
    } else {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "HEAD %s failed with HTTP Error: %s", address.c_str(), http.errorToString(httpCode).c_str());
        Logger.error(MAINLOG, buffer);
    }
    http.end();
    //Logger.debug(MAINLOG, "HEAD %s result: %s (code: %d)", address.c_str(), success ? "success" : "failed", httpCode);
    return success;
}

void MonitoringService::_handleStatusChange(const String& name, bool online) {
    delay(250);
    String msg;
    msg.reserve(256);
    msg = "⚠️ Status Change Alert\n";
    msg += "Entity: **" + name + "**\n";
    msg += "New status: " + String(online ? "✅ **ONLINE**" : "❌ **OFFLINE**");
    notificationService.send(msg, online ? DISCORD_COLOR_GREEN : DISCORD_COLOR_RED);
}

MonitoringService& monitoringService = MonitoringService::getInstance();
