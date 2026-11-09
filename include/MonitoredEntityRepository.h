#ifndef _MONITORED_ENTITY_REPOSITORY_H_
#define _MONITORED_ENTITY_REPOSITORY_H_

#include <vector>
#include <Arduino.h>

#ifdef UNIT_TEST
#include "freertos/FreeRTOS.h"
#endif

#include "MonitoredEntity.h"
#include "Storage.h"

class MonitoredEntityRepository {
public:
    MonitoredEntityRepository(const MonitoredEntityRepository&) = delete;
    MonitoredEntityRepository& operator=(const MonitoredEntityRepository&) = delete;
    static MonitoredEntityRepository& getInstance();
    bool load();
    bool save();
    void addEntity(MonitoredEntity& entity);
    bool updateEntity(const String &name, const MonitoredEntity& updatedEntity);
    bool removeEntity(const String& name);
    bool getEntity(const String& name, MonitoredEntity& outEntity);
    bool hasEntity(const String& name);
    std::vector<MonitoredEntity> getEntitiesSnapshot();
    std::vector<MonitoredEntity> getEntities() { return getEntitiesSnapshot(); }
    bool updateCheckResult(const String& name, bool success, bool& statusChanged, bool& newStatus, bool& notify);
    void getCounts(uint8_t& online, uint8_t& offline, unsigned int& total);
    unsigned int size();
    void fillJsonArray(JsonArray& array);
    SemaphoreHandle_t getMutex() { return _mutex; }
private:
    SemaphoreHandle_t _mutex;
    std::vector<MonitoredEntity> _entities;
    MonitoredEntityRepository();
    MonitoredEntity* getEntityInternal(const String& name);
};

extern MonitoredEntityRepository& monitoredEntityRepository;

#endif // _MONITORED_ENTITY_REPOSITORY_H_
