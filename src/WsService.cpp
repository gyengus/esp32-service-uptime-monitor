#include "WsService.h"
#include "WsLogStream.h"
#include <ArduinoJson.h>
#include <Elog.h>
#include <WiFi.h>
#include "driver/temperature_sensor.h"
#include "MonitoredEntityRepository.h"
#include "MonitoringService.h"
#include "Config.h"
#include "PSRAMAllocator.h"
#include "SystemInfo.h"
#include "globals.h"
#include "version.h"

static PSRAMAllocator psramAllocator;

WsService& WsService::getInstance() {
    static WsService instance;
    return instance;
}

WsService::WsService() : _ws("/ws"), _eventQueue(nullptr), _logQueue(nullptr), _initialized(false) {
    _eventQueue = xQueueCreate(32, sizeof(WsQueueItem));
    if (_eventQueue == nullptr) {
        Logger.error(MAINLOG, "Failed to create WebSocket event queue");
    }
    _logQueue = xQueueCreate(20, 384);
    if (_logQueue == nullptr) {
        Logger.error(MAINLOG, "Failed to create WebSocket log queue");
    }
}

WsService::~WsService() {
    if (_eventQueue != nullptr) {
        vQueueDelete(_eventQueue);
        _eventQueue = nullptr;
    }
    if (_logQueue != nullptr) {
        vQueueDelete(_logQueue);
        _logQueue = nullptr;
    }
}

void WsService::init(AsyncWebServer& server) {
    if (_initialized) return;

    _ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        this->onEvent(server, client, type, arg, data, len);
    });

    server.addHandler(&_ws);
    _initialized = true;
    Logger.info(MAINLOG, "WebSocket service initialized at /ws");
}

void WsService::cleanupClients() {
    if (_initialized) {
        _ws.cleanupClients();
    }
}

void WsService::closeAll() {
    if (_initialized) {
        _ws.closeAll();
    }
}

size_t WsService::count() const {
    return _ws.count();
}

bool WsService::hasActiveClients() const {
    return _ws.count() > 0;
}

void WsService::queueEntityUpdate(const MonitoredEntity& entity) {
    if (!hasActiveClients() || _eventQueue == nullptr) return;

    WsQueueItem item;
    memset(&item, 0, sizeof(item));
    item.type = WsEventType::ENTITY_UPDATE;
    strncpy(item.name, entity.getName().c_str(), sizeof(item.name) - 1);
    strncpy(item.address, entity.getAddress().c_str(), sizeof(item.address) - 1);
    strncpy(item.icon, entity.getIcon().c_str(), sizeof(item.icon) - 1);
    item.isOnline = entity.isOnline();
    item.notification = entity.getNotification();
    item.disabled = entity.isDisabled();
    item.lastChanged = entity.getLastChanged();
    item.failCount = entity.getFailCount();

    xQueueSend(_eventQueue, &item, 0);
}

void WsService::queueEntityDeleted(const String& name) {
    if (!hasActiveClients() || _eventQueue == nullptr) return;

    WsQueueItem item;
    memset(&item, 0, sizeof(item));
    item.type = WsEventType::ENTITY_DELETED;
    strncpy(item.name, name.c_str(), sizeof(item.name) - 1);

    xQueueSend(_eventQueue, &item, 0);
}

void WsService::queueSummary(uint8_t online, uint8_t offline, unsigned int total) {
    if (!hasActiveClients() || _eventQueue == nullptr) return;

    WsQueueItem item;
    memset(&item, 0, sizeof(item));
    item.type = WsEventType::SUMMARY_UPDATE;
    item.online = online;
    item.offline = offline;
    item.total = total;

    xQueueSend(_eventQueue, &item, 0);
}

void WsService::queueSysinfo() {
    if (!hasActiveClients() || _eventQueue == nullptr) return;

    WsQueueItem item;
    memset(&item, 0, sizeof(item));
    item.type = WsEventType::SYSINFO_UPDATE;

    xQueueSend(_eventQueue, &item, 0);
}

void WsService::queueLogLine(const char* line) {
    if (!line || line[0] == '\0') return;
    if (!hasLogSubscribers() || _logQueue == nullptr) return;

    char buffer[384];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    xQueueSend(_logQueue, buffer, 0);
}

void WsService::processEvents() {
    if (_logQueue != nullptr) {
        if (!hasLogSubscribers()) {
            char dummy[384];
            while (xQueueReceive(_logQueue, dummy, 0)) {}
        } else {
            char lineBuf[384];
            int count = 0;
            while (xQueueReceive(_logQueue, lineBuf, 0) && count++ < 16) {
                broadcastLogLine(lineBuf);
            }
        }
    }

    if (!hasActiveClients() || _eventQueue == nullptr) {
        // Drain queue if there are no listeners
        WsQueueItem dummy;
        while (_eventQueue && xQueueReceive(_eventQueue, &dummy, 0)) {}
        return;
    }

    WsQueueItem item;
    while (xQueueReceive(_eventQueue, &item, 0)) {
        handleQueueItem(item);
    }
}

void WsService::handleQueueItem(const WsQueueItem& item) {
    switch (item.type) {
        case WsEventType::ENTITY_UPDATE: {
            JsonDocument doc;
            doc["type"] = "entity_update";
            JsonObject ent = doc["entity"].to<JsonObject>();
            ent["name"] = item.name;
            ent["address"] = item.address;
            ent["icon"] = item.icon;
            ent["isOnline"] = item.isOnline;
            ent["notification"] = item.notification;
            ent["disabled"] = item.disabled;
            ent["lastChanged"] = item.lastChanged;
            ent["failCount"] = item.failCount;

            String out;
            serializeJson(doc, out);
            _ws.textAll(out);
            break;
        }
        case WsEventType::ENTITY_DELETED: {
            JsonDocument doc;
            doc["type"] = "entity_delete";
            doc["name"] = item.name;

            String out;
            serializeJson(doc, out);
            _ws.textAll(out);
            break;
        }
        case WsEventType::SUMMARY_UPDATE: {
            JsonDocument doc;
            doc["type"] = "summary";
            JsonObject data = doc["data"].to<JsonObject>();
            data["total"] = item.total;
            data["online"] = item.online;
            data["offline"] = item.offline;

            String out;
            serializeJson(doc, out);
            _ws.textAll(out);
            break;
        }
        case WsEventType::SYSINFO_UPDATE: {
            broadcastSysinfo();
            break;
        }
    }
}

void WsService::broadcastEntityUpdate(const MonitoredEntity& entity) {
    if (!hasActiveClients()) return;

    JsonDocument doc;
    doc["type"] = "entity_update";
    JsonObject ent = doc["entity"].to<JsonObject>();
    ent["name"] = entity.getName();
    ent["address"] = entity.getAddress();
    ent["icon"] = entity.getIcon();
    ent["isOnline"] = entity.isOnline();
    ent["notification"] = entity.getNotification();
    ent["disabled"] = entity.isDisabled();
    ent["lastChanged"] = entity.getLastChanged();
    ent["failCount"] = entity.getFailCount();

    String out;
    serializeJson(doc, out);
    _ws.textAll(out);
}

void WsService::broadcastEntityDeleted(const String& name) {
    if (!hasActiveClients()) return;

    JsonDocument doc;
    doc["type"] = "entity_delete";
    doc["name"] = name;

    String out;
    serializeJson(doc, out);
    _ws.textAll(out);
}

void WsService::broadcastSummary() {
    if (!hasActiveClients()) return;
    uint8_t online = 0, offline = 0;
    unsigned int total = 0;
    monitoredEntityRepository.getCounts(online, offline, total);
    broadcastSummary(online, offline, total);
}

void WsService::broadcastSummary(uint8_t online, uint8_t offline, unsigned int total) {
    if (!hasActiveClients()) return;

    JsonDocument doc;
    doc["type"] = "summary";
    JsonObject data = doc["data"].to<JsonObject>();
    data["total"] = total;
    data["online"] = online;
    data["offline"] = offline;

    String out;
    serializeJson(doc, out);
    _ws.textAll(out);
}

void WsService::broadcastSysinfo() {
    if (!hasActiveClients()) return;

    JsonDocument doc;
    doc["type"] = "sysinfo";
    JsonObject data = doc["data"].to<JsonObject>();
    SystemInfo::fillBasicSysinfo(data);

    String out;
    serializeJson(doc, out);
    _ws.textAll(out);
}

void WsService::sendInitState(AsyncWebSocketClient* client) {
    if (!client) return;

    JsonDocument doc;
    doc["type"] = "init";

    JsonArray array = doc["entities"].to<JsonArray>();
    auto entities = monitoredEntityRepository.getEntitiesSnapshot();
    for (const auto& entity : entities) {
        JsonObject item = array.add<JsonObject>();
        item["name"] = entity.getName();
        item["address"] = entity.getAddress();
        item["icon"] = entity.getIcon();
        item["isOnline"] = entity.isOnline();
        item["notification"] = entity.getNotification();
        item["disabled"] = entity.isDisabled();
        item["lastChanged"] = entity.getLastChanged();
        item["failCount"] = entity.getFailCount();
    }

    uint8_t online = 0, offline = 0;
    unsigned int total = 0;
    monitoredEntityRepository.getCounts(online, offline, total);

    JsonObject sum = doc["summary"].to<JsonObject>();
    sum["total"] = total;
    sum["online"] = online;
    sum["offline"] = offline;

    JsonObject sys = doc["sysinfo"].to<JsonObject>();
    SystemInfo::fillBasicSysinfo(sys);

    doc["logLevel"] = config.getConfig().logLevel;
    doc["logLevelName"] = Config::logLevelToString(config.getConfig().logLevel);

    String out;
    serializeJson(doc, out);
    bool ok = client->text(out);
    Logger.info(MAINLOG, "Sent init state to WS client #%u (%d bytes, success: %s)", client->id(), out.length(), ok ? "true" : "false");
}

void WsService::broadcastLogLine(const char* line) {
    if (!line || _logSubscribers.empty()) return;

    JsonDocument doc;
    doc["type"] = "log";
    doc["line"] = line;

    String out;
    serializeJson(doc, out);

    for (auto it = _logSubscribers.begin(); it != _logSubscribers.end(); ) {
        AsyncWebSocketClient* c = _ws.client(*it);
        if (c && c->status() == WS_CONNECTED) {
            c->text(out);
            ++it;
        } else {
            it = _logSubscribers.erase(it);
        }
    }
}

void WsService::addLogSubscriber(uint32_t clientId) {
    for (uint32_t id : _logSubscribers) {
        if (id == clientId) return;
    }
    _logSubscribers.push_back(clientId);
}

void WsService::removeLogSubscriber(uint32_t clientId) {
    for (auto it = _logSubscribers.begin(); it != _logSubscribers.end(); ++it) {
        if (*it == clientId) {
            _logSubscribers.erase(it);
            return;
        }
    }
}

bool WsService::hasLogSubscribers() const {
    return !_logSubscribers.empty();
}

void WsService::setLogLevel(uint8_t level) {
    if (level > 11) level = DEFAULT_LOG_LEVEL;

    Logger.info(MAINLOG, "Changing log level to %s (%u)", Config::logLevelToString(level), level);
    Logger.setSerialLogLevel(MAINLOG, level, Serial);
    Logger.setSerialLogLevel(MAINLOG, level, wsLogStream);

    config.updateLogLevel(level);

    JsonDocument doc;
    doc["type"] = "log_level";
    doc["level"] = level;
    doc["name"] = Config::logLevelToString(level);

    String out;
    serializeJson(doc, out);
    _ws.textAll(out);
}

void WsService::handleIncomingData(AsyncWebSocketClient* client, uint8_t* data, size_t len) {
    if (!client || !data || len == 0) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (const char*)data, len);
    if (err) {
        return;
    }

    const char* type = doc["type"] | "";
    if (strcmp(type, "subscribe_logs") == 0) {
        bool enable = doc["enabled"] | false;
        if (enable) {
            addLogSubscriber(client->id());
            Logger.debug(MAINLOG, "WS client #%u subscribed to logs", client->id());
        } else {
            removeLogSubscriber(client->id());
            Logger.debug(MAINLOG, "WS client #%u unsubscribed from logs", client->id());
        }
    } else if (strcmp(type, "set_log_level") == 0) {
        uint8_t newLevel = config.getConfig().logLevel;
        if (doc["level"].is<int>() || doc["level"].is<uint8_t>()) {
            newLevel = doc["level"].as<uint8_t>();
        } else if (doc["level"].is<const char*>()) {
            newLevel = Config::stringToLogLevel(doc["level"].as<const char*>(), newLevel);
        }
        setLogLevel(newLevel);
    }
}

void WsService::onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Logger.info(MAINLOG, "WebSocket client #%u connected", client->id());
        sendInitState(client);
    } else if (type == WS_EVT_DISCONNECT) {
        removeLogSubscriber(client->id());
        Logger.info(MAINLOG, "WebSocket client #%u disconnected", client->id());
    } else if (type == WS_EVT_DATA) {
        handleIncomingData(client, data, len);
    }
}

WsService& wsService = WsService::getInstance();
