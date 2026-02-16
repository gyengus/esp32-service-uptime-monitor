#include "MonitoringService.h"
#include <Elog.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "Config.h"
#include "MonitoredEntityRepository.h"
#include "NotificationService.h"

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
    std::vector<String> names;
    if (xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY)) {
        for (const auto& e : monitoredEntityRepository.getEntities()) {
            names.push_back(e.getName());
        }
        xSemaphoreGive(monitoredEntityRepository.getMutex());
    }

    bool mustSave = false;
    uint8_t onlineCount = 0;
    uint8_t offlineCount = 0;

    for (const String& name : names) {
        MonitoredEntity* entity = monitoredEntityRepository.getEntityInternal(name);
        if (!entity) continue;
        bool oldStatus = entity->isOnline();
        bool currentMeasurement = check(*entity);

        if (currentMeasurement) {
            entity->resetFailCount();
            onlineCount++;
            if (!oldStatus) {
                if (xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY)) {
                    _handleStatusChange(entity, true);
                    xSemaphoreGive(monitoredEntityRepository.getMutex());
                }
                mustSave = true;
            }
        } else {
            entity->incrementFailCount();
            
            if ((!entity->getLastChanged() || oldStatus) && entity->getFailCount() >= HTTP_REQUEST_RETRIES) {
                if (xSemaphoreTake(monitoredEntityRepository.getMutex(), portMAX_DELAY)) {
                    _handleStatusChange(entity, false);
                    xSemaphoreGive(monitoredEntityRepository.getMutex());
                }
                mustSave = true;
            } else if (!oldStatus) {
                offlineCount++;
            } else {
                onlineCount--;
            }

        }
        //Logger.debug(MAINLOG, "%s, %d -> %d", entity->getName().c_str(), oldStatus, currentOnlineStatus);
    }

    if (mustSave) {
        monitoredEntityRepository.save();
    }

    _online = onlineCount;
    _offline = offlineCount;
    return true;
}

bool MonitoringService::check(const MonitoredEntity& entity) {
    if (entity.getType() == HEAD) {
        return _doHead(entity.getAddress());
    } else if (entity.getType() == PING) {
        return _doPing(entity.getAddress());
    } else {
        Logger.error(MAINLOG, "Unknown entity type: %d", entity.getType());
        return false;
    }
    return true;
}

bool MonitoringService::_doPing(const String& address) {
    return false;
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

void MonitoringService::_handleStatusChange(MonitoredEntity *entity, bool online) {
    //Logger.info(MAINLOG, "STATUS CHANGE: %s (%d -> %d)", name.c_str(), oldStatus, currentOnlineStatus);
    entity->setOnlineStatus(online);
    if (entity->getNotification() && entity->getLastChanged() != 0) {
        delay(250);
        String msg;
        msg.reserve(256);
        msg = "⚠️ *Status Change Alert*\n";
        msg += "Entity: *" + entity->getName() + "*\n";
        msg += "New status: " + String(online ? "✅ *ONLINE*" : "❌ *OFFLINE*");
        notificationService.send(msg);
    }
}

MonitoringService& monitoringService = MonitoringService::getInstance();
