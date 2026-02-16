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

MonitoredEntity* MonitoredEntityRepository::getEntity(const String& name) {
    MonitoredEntity* found = nullptr;
    if (xSemaphoreTake(_mutex, portMAX_DELAY)) {
        found = getEntityInternal(name);
        xSemaphoreGive(_mutex);
    }
    return found;
}

std::vector<MonitoredEntity>& MonitoredEntityRepository::getEntities() {
    return _entities;
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
