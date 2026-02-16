#ifndef _MONITORED_ENTITY_REPOSITORY_H_
#define _MONITORED_ENTITY_REPOSITORY_H_

#include <vector>
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
    MonitoredEntity* getEntity(const String& name);
    MonitoredEntity* getEntityInternal(const String& name);
    std::vector<MonitoredEntity>& getEntities();
    unsigned int size();
    void fillJsonArray(JsonArray& array);
    SemaphoreHandle_t getMutex() { return _mutex; }
private:
    SemaphoreHandle_t _mutex;
    std::vector<MonitoredEntity> _entities;
    MonitoredEntityRepository();
};

extern MonitoredEntityRepository& monitoredEntityRepository;

#endif // _MONITORED_ENTITY_REPOSITORY_H_
