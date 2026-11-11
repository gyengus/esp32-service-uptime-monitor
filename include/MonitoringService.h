#ifndef _MONITORING_SERVICE_H_
#define _MONITORING_SERVICE_H_

#include <Arduino.h>
#include <atomic>
#include <ctime>
#include "MonitoredEntity.h"

class MonitoringService {
public:
    MonitoringService(const MonitoringService&) = delete;
    MonitoringService& operator=(const MonitoringService&) = delete;
    static MonitoringService& getInstance();
    void init();
    String summary();
    bool check(const MonitoredEntity& entity);
    bool checkAll();
    uint8_t getOfflineCount() { return _offline; }
    uint32_t getLastCheckDurationMs() const { return _lastCheckDurationMs.load(); }
    time_t getLastCheckTime() const { return _lastCheckTime.load(); }
private:
    uint8_t _entities = 0;
    uint8_t _offline = 0;
    uint8_t _online = 0;
    std::atomic<uint32_t> _lastCheckDurationMs{0};
    std::atomic<time_t> _lastCheckTime{0};
    MonitoringService();
    bool _doHead(const String& address);
    void _handleStatusChange(const String& name, bool online);
};

extern MonitoringService& monitoringService;

#endif // _MONITORING_SERVICE_H_