#include "WsService.h"
#include <ArduinoJson.h>
#include <Elog.h>
#include <WiFi.h>
#include "driver/temperature_sensor.h"
#include "MonitoredEntityRepository.h"
#include "MonitoringService.h"
#include "Config.h"
#include "PSRAMAllocator.h"
#include "globals.h"
#include "version.h"

static PSRAMAllocator psramAllocator;

WsService& WsService::getInstance() {
    static WsService instance;
    return instance;
}

WsService::WsService() : _ws("/ws"), _eventQueue(nullptr), _initialized(false) {
    _eventQueue = xQueueCreate(32, sizeof(WsQueueItem));
    if (_eventQueue == nullptr) {
        Logger.error(MAINLOG, "Failed to create WebSocket event queue");
    }
}

WsService::~WsService() {
    if (_eventQueue != nullptr) {
        vQueueDelete(_eventQueue);
        _eventQueue = nullptr;
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

void WsService::processEvents() {
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

    float tsens_out = 0.0;
    esp_err_t error = temperature_sensor_get_celsius(temp_sensor, &tsens_out);
    if (error == ESP_OK) {
        data["temperature"] = tsens_out;
    }
    data["freeHeap"] = ESP.getFreeHeap();
    data["rssi"] = WiFi.RSSI();
    data["uptime"] = millis() / 1000;
    data["version"] = FIRMWARE_VERSION;
    data["buildDate"] = BUILD_TIMESTAMP;
    data["commit"] = COMMIT_HASH;
    data["configEnabled"] = isConfigFunctionsEnabled;

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
    float tsens_out = 0.0;
    esp_err_t error = temperature_sensor_get_celsius(temp_sensor, &tsens_out);
    if (error == ESP_OK) {
        sys["temperature"] = tsens_out;
    }
    sys["freeHeap"] = ESP.getFreeHeap();
    sys["rssi"] = WiFi.RSSI();
    sys["uptime"] = millis() / 1000;
    sys["version"] = FIRMWARE_VERSION;
    sys["buildDate"] = BUILD_TIMESTAMP;
    sys["commit"] = COMMIT_HASH;
    sys["configEnabled"] = isConfigFunctionsEnabled;

    String out;
    serializeJson(doc, out);
    bool ok = client->text(out);
    Logger.info(MAINLOG, "Sent init state to WS client #%u (%d bytes, success: %s)", client->id(), out.length(), ok ? "true" : "false");
}

void WsService::onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Logger.info(MAINLOG, "WebSocket client #%u connected", client->id());
        sendInitState(client);
    } else if (type == WS_EVT_DISCONNECT) {
        Logger.info(MAINLOG, "WebSocket client #%u disconnected", client->id());
    } else if (type == WS_EVT_DATA) {
        Logger.debug(MAINLOG, "WebSocket client #%u sent data (%u bytes)", client->id(), len);
        broadcastSysinfo();
    }
}

WsService& wsService = WsService::getInstance();
