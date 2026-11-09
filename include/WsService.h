#ifndef _WS_SERVICE_H_
#define _WS_SERVICE_H_

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include "MonitoredEntity.h"

#ifdef UNIT_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif

enum class WsEventType : uint8_t {
    ENTITY_UPDATE,
    ENTITY_DELETED,
    SUMMARY_UPDATE,
    SYSINFO_UPDATE
};

struct WsQueueItem {
    WsEventType type;
    char name[64];
    char address[128];
    char icon[48];
    bool isOnline;
    bool notification;
    bool disabled;
    time_t lastChanged;
    uint8_t failCount;
    uint8_t online;
    uint8_t offline;
    unsigned int total;
};

class WsService {
public:
    WsService(const WsService&) = delete;
    WsService& operator=(const WsService&) = delete;

    static WsService& getInstance();

    void init(AsyncWebServer& server);
    void cleanupClients();
    void closeAll();
    size_t count() const;
    bool hasActiveClients() const;

    // Thread-safe queueing methods (callable from any task/core)
    void queueEntityUpdate(const MonitoredEntity& entity);
    void queueEntityDeleted(const String& name);
    void queueSummary(uint8_t online, uint8_t offline, unsigned int total);
    void queueSysinfo();
    void queueLogLine(const char* line);

    // Process queued events on Core 1 / main loop
    void processEvents();

    // Direct broadcasts (called from Core 1 / API handlers or loop)
    void broadcastEntityUpdate(const MonitoredEntity& entity);
    void broadcastEntityDeleted(const String& name);
    void broadcastSummary();
    void broadcastSummary(uint8_t online, uint8_t offline, unsigned int total);
    void broadcastSysinfo();
    void broadcastLogLine(const char* line);

    // Log streaming subscriber management & log level control
    void addLogSubscriber(uint32_t clientId);
    void removeLogSubscriber(uint32_t clientId);
    bool hasLogSubscribers() const;
    void setLogLevel(uint8_t level);

    // Send initial snapshot to a specific client on connect
    void sendInitState(AsyncWebSocketClient* client);

    AsyncWebSocket& getWebSocket() { return _ws; }

private:
    WsService();
    ~WsService();

    AsyncWebSocket _ws;
    QueueHandle_t _eventQueue;
    QueueHandle_t _logQueue;
    std::vector<uint32_t> _logSubscribers;
    bool _initialized;

    void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    void handleIncomingData(AsyncWebSocketClient* client, uint8_t* data, size_t len);
    void handleQueueItem(const WsQueueItem& item);
};

extern WsService& wsService;

#endif // _WS_SERVICE_H_
