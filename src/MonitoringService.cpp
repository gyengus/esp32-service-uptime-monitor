#include "MonitoringService.h"
#include <Elog.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include "Config.h"
#include "MonitoredEntityRepository.h"
#include "NotificationService.h"
#include "WsService.h"
#include "globals.h"
#include "AddressValidator.h"

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
    unsigned long startMs = millis();
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

    _lastCheckDurationMs.store(static_cast<uint32_t>(millis() - startMs));
    _lastCheckTime.store(time(nullptr));

    return true;
}

bool MonitoringService::check(const MonitoredEntity& entity) {
    return _doHead(entity.getAddress());
}

static String extractHost(const String& address) {
    String host = address;
    int protoIdx = host.indexOf("://");
    if (protoIdx != -1) {
        host = host.substring(protoIdx + 3);
    }
    int slashIdx = host.indexOf('/');
    if (slashIdx != -1) {
        host = host.substring(0, slashIdx);
    }
    int colonIdx = host.indexOf(':');
    if (colonIdx != -1) {
        host = host.substring(0, colonIdx);
    }
    return host;
}

static bool isIpAddress(const String& host) {
    IPAddress ip;
    return ip.fromString(host);
}

static String stripQueryAndFragment(const String& url) {
    int queryIdx = url.indexOf('?');
    int fragmentIdx = url.indexOf('#');
    int cutIdx = -1;
    if (queryIdx != -1 && fragmentIdx != -1) {
        cutIdx = (queryIdx < fragmentIdx) ? queryIdx : fragmentIdx;
    } else if (queryIdx != -1) {
        cutIdx = queryIdx;
    } else if (fragmentIdx != -1) {
        cutIdx = fragmentIdx;
    }
    if (cutIdx != -1) {
        return url.substring(0, cutIdx);
    }
    return url;
}

bool MonitoringService::_doHead(const String& address) {
    bool success = false;
    String host = extractHost(address);
    bool isIp = isIpAddress(host);
    bool dnsFailed = false;
    String sanitizedAddress = stripQueryAndFragment(address);

    if (isIp) {
        IPAddress ip;
        if (ip.fromString(host) && AddressValidator::isForbiddenIPv4(ip[0], ip[1], ip[2], ip[3])) {
            char buffer[160];
            snprintf(buffer, sizeof(buffer), "HEAD %s blocked: forbidden IP %s", sanitizedAddress.c_str(), host.c_str());
            Logger.error(MAINLOG, "%s", buffer);
            return false;
        }
    } else {
        IPAddress resolvedIp;
        if (WiFi.hostByName(host.c_str(), resolvedIp) != 1 || resolvedIp == IPAddress(0, 0, 0, 0)) {
            dnsFailed = true;
        } else if (AddressValidator::isForbiddenIPv4(resolvedIp[0], resolvedIp[1], resolvedIp[2], resolvedIp[3])) {
            char buffer[160];
            snprintf(buffer, sizeof(buffer), "HEAD %s blocked: host %s resolved to forbidden address %u.%u.%u.%u",
                     sanitizedAddress.c_str(), host.c_str(), resolvedIp[0], resolvedIp[1], resolvedIp[2], resolvedIp[3]);
            Logger.error(MAINLOG, "%s", buffer);
            return false;
        }
    }

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
        char buffer[160];
        if (!isIp && dnsFailed) {
            snprintf(buffer, sizeof(buffer), "HEAD %s failed: host not found (DNS/mDNS resolution failed)", sanitizedAddress.c_str());
        } else {
            snprintf(buffer, sizeof(buffer), "HEAD %s failed with HTTP Error: %s", sanitizedAddress.c_str(), http.errorToString(httpCode).c_str());
        }
        Logger.error(MAINLOG, "%s", buffer);
    }
    http.end();
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
