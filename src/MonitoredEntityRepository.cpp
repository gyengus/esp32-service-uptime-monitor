#include "MonitoredEntityRepository.h"

#include <ArduinoJson.h>
#include <Elog.h>
#include <FS.h>

#include "Config.h"
#include "Storage.h"
#include "MonitoredEntity.h"
#include "PSRAMAllocator.h"

static PSRAMAllocator psramAllocator;

MonitoredEntityRepository& MonitoredEntityRepository::getInstance() {
    static MonitoredEntityRepository instance;
    return instance;
}

MonitoredEntityRepository::MonitoredEntityRepository() {
    _mutex = xSemaphoreCreateMutex();
}

bool MonitoredEntityRepository::load() {
    if (!storage.fileExists(ENTITIES_FILE_PATH)) {
        Logger.info(MAINLOG, "Entities file not found, starting with empty list: %s", ENTITIES_FILE_PATH);
        _entities.clear();
        return true; 
    }

    File file = storage.getFile(ENTITIES_FILE_PATH, FILE_READ);
    if (!file || file.size() == 0) {
        Logger.info(MAINLOG, "Entities file not found or empty.");
        _entities.clear();
        return true; 
    }
    
    JsonDocument doc(&psramAllocator); 
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Logger.error(MAINLOG, "JSON parse error in %s: %s", ENTITIES_FILE_PATH, error.c_str());
        return false;
    }

    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        _entities.clear();
        if (doc.is<JsonArray>()) {
            for (JsonObject obj : doc.as<JsonArray>()) {
                MonitoredEntity entity;
                if (entity.fromJsonObject(obj)) {
                    _entities.push_back(entity);
                }
            }
        }
        xSemaphoreGive(_mutex);
    }

    Logger.debug(MAINLOG, "Loaded %d monitored entities.", _entities.size());
    return true;
}

bool MonitoredEntityRepository::save() {
    bool success = false;

    JsonDocument doc(&psramAllocator);
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        JsonArray array = doc.to<JsonArray>();
        for (const auto& entity : _entities) {
            JsonObject obj = array.add<JsonObject>();
            entity.fillJson(obj);
        }
        xSemaphoreGive(_mutex);
    }

    File file = storage.getFile(ENTITIES_FILE_PATH, FILE_WRITE);
    if (file) {
        if (serializeJson(doc, file) > 0) {
            Logger.debug(MAINLOG, "Saved %d monitored entities to %s.", _entities.size(), ENTITIES_FILE_PATH);
            success = true;
        }
        file.close();
    } else {
        Logger.error(MAINLOG, "Failed to open file for writing: %s", ENTITIES_FILE_PATH);
    }  
    
    return success;
}

void MonitoredEntityRepository::addEntity(MonitoredEntity& entity) {
    entity.resetFailCount();
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        _entities.push_back(entity);
        xSemaphoreGive(_mutex);
    }
    save();
}

bool MonitoredEntityRepository::updateEntity(const String &name, const MonitoredEntity& updatedEntity) {
    bool success = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        MonitoredEntity* entity = getEntityInternal(name);
        if (entity) {
            *entity = updatedEntity;
            entity->resetFailCount();
            success = true;
        }
        xSemaphoreGive(_mutex);
    }
    if (success) save();
    return success;
}

bool MonitoredEntityRepository::removeEntity(const String& name) {
    bool success = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        for (auto it = _entities.begin(); it != _entities.end(); ++it) {
            if (it->getName() == name) {
                _entities.erase(it);
                success = true;
                break;
            }
        }
        xSemaphoreGive(_mutex);
    }
    if (success) save();
    return success;
}

MonitoredEntity* MonitoredEntityRepository::getEntityInternal(const String& name) {
    MonitoredEntity* found = nullptr;
    for (auto& entity : _entities) {
        if (entity.getName() == name) {
            found = &entity;
            break;
        }
    }
    return found;
}

bool MonitoredEntityRepository::getEntity(const String& name, MonitoredEntity& outEntity) {
    bool found = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        MonitoredEntity* ptr = getEntityInternal(name);
        if (ptr != nullptr) {
            outEntity = *ptr;
            found = true;
        }
        xSemaphoreGive(_mutex);
    }
    return found;
}

bool MonitoredEntityRepository::hasEntity(const String& name) {
    bool found = false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        found = (getEntityInternal(name) != nullptr);
        xSemaphoreGive(_mutex);
    }
    return found;
}

std::vector<MonitoredEntity> MonitoredEntityRepository::getEntitiesSnapshot() {
    std::vector<MonitoredEntity> snapshot;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        snapshot = _entities;
        xSemaphoreGive(_mutex);
    }
    return snapshot;
}

bool MonitoredEntityRepository::updateCheckResult(const String& name, bool success, bool& statusChanged, bool& newStatus, bool& notify) {
    statusChanged = false;
    newStatus = false;
    notify = false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        MonitoredEntity* entity = getEntityInternal(name);
        if (entity && !entity->isDisabled()) {
            bool oldStatus = entity->isOnline();
            if (success) {
                entity->resetFailCount();
                if (!oldStatus) {
                    entity->setOnlineStatus(true);
                    statusChanged = true;
                    newStatus = true;
                    notify = entity->getNotification() && (entity->getLastChanged() != 0);
                }
            } else {
                entity->incrementFailCount();
                if ((!entity->getLastChanged() || oldStatus) && entity->getFailCount() >= HTTP_REQUEST_RETRIES) {
                    entity->setOnlineStatus(false);
                    statusChanged = true;
                    newStatus = false;
                    notify = entity->getNotification() && (entity->getLastChanged() != 0);
                }
            }
        }
        xSemaphoreGive(_mutex);
    }
    return statusChanged;
}

void MonitoredEntityRepository::getCounts(uint8_t& online, uint8_t& offline, unsigned int& total) {
    online = 0;
    offline = 0;
    total = 0;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        total = _entities.size();
        for (const auto& entity : _entities) {
            if (entity.isDisabled()) continue;
            if (entity.isOnline()) {
                online++;
            } else if (entity.getLastChanged() != 0) {
                offline++;
            }
        }
        xSemaphoreGive(_mutex);
    }
}

unsigned int MonitoredEntityRepository::size() {
    unsigned int s = 0;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        s = _entities.size();
        xSemaphoreGive(_mutex);
    }
    return s;
}

void MonitoredEntityRepository::fillJsonArray(JsonArray& array) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        for (const auto& entity : _entities) {
            JsonObject obj = array.add<JsonObject>();
            entity.fillJson(obj);
        }
        xSemaphoreGive(_mutex);
    }
}

MonitoredEntityRepository& monitoredEntityRepository = MonitoredEntityRepository::getInstance();
