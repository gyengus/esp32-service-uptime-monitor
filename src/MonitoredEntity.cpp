#include "MonitoredEntity.h"
#include "AddressValidator.h"

MonitoredEntity::MonitoredEntity(const String& name, const String& address, const String& icon, bool notify)
    : _name(name), _address(address), _icon(icon), _notification(notify),
      _isOnline(false), _lastChanged(0), _disabled(false), _failCount(0) {}

MonitoredEntity::MonitoredEntity() : _isOnline(false), _lastChanged(0), _notification(true), _disabled(false), _failCount(0) { }

// --- Getters ---
String MonitoredEntity::getName() const { return _name; }
String MonitoredEntity::getAddress() const { return _address; }
String MonitoredEntity::getIcon() const { return _icon; }
bool MonitoredEntity::isOnline() const { return _isOnline; }
bool MonitoredEntity::getNotification() const { return _notification; }
bool MonitoredEntity::isDisabled() const { return _disabled; }
time_t MonitoredEntity::getLastChanged() const { return _lastChanged; }

// --- Setters ---
void MonitoredEntity::setName(const String &name) { _name = name; }
void MonitoredEntity::setAddress(const String &addr) { _address = addr; }
void MonitoredEntity::setIcon(const String &icon) { _icon = icon; }
void MonitoredEntity::setNotification(bool notify) { _notification = notify; }
void MonitoredEntity::setDisabled(bool disabled) { _disabled = disabled; }
void MonitoredEntity::setLastChanged(time_t lastChanged) { _lastChanged = lastChanged; }

void MonitoredEntity::setOnlineStatus(bool online) {
    if (_isOnline != online || _lastChanged == 0) {
        _isOnline = online;
        _lastChanged = time(NULL);
        if (online) _failCount = 0;
    }
}

String MonitoredEntity::toJson() const {
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    fillJson(obj);
    String output;
    serializeJson(doc, output);
    return output;
}

void MonitoredEntity::fillJson(JsonObject& doc) const {
    doc["name"] = _name;
    doc["address"] = _address;
    doc["icon"] = _icon;
    doc["isOnline"] = _isOnline;
    doc["notification"] = _notification;
    doc["disabled"] = _disabled;
    doc["lastChanged"] = (uint64_t)_lastChanged;
}

bool MonitoredEntity::isValidAddress(const String& addr) {
    return AddressValidator::isValidMonitoredUrl(addr);
}

bool MonitoredEntity::isValidForUserInput() const {
    return AddressValidator::isValidEntityName(_name) &&
           AddressValidator::isValidMonitoredUrl(_address) &&
           AddressValidator::isValidEntityIcon(_icon);
}

// Deliberately lenient: also used when loading persisted entities, which must never be dropped.
// User-supplied entities are checked with isValidForUserInput() in the API layer.
bool MonitoredEntity::fromJsonObject(const JsonObject& doc) {
    if (!doc["name"].is<const char*>() || !doc["address"].is<const char*>()) {
        return false; 
    }

    _name = doc["name"].as<String>();
    _address = doc["address"].as<String>();
    if (doc["icon"].is<const char*>()) {
        _icon = doc["icon"].as<String>();
    }

    if (doc["notification"].is<bool>()) _notification = doc["notification"].as<bool>();
    if (doc["disabled"].is<bool>()) _disabled = doc["disabled"].as<bool>();
    if (doc["isOnline"].is<bool>()) _isOnline = doc["isOnline"].as<bool>();

    if (doc["lastChanged"].is<uint64_t>() || doc["lastChanged"].is<uint32_t>()) {
        _lastChanged = (time_t)doc["lastChanged"].as<uint64_t>();
    }

    return true;
}

bool MonitoredEntity::fromJson(const String& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) return false;
    
    JsonObject obj = doc.as<JsonObject>();
    return fromJsonObject(obj);
}

MonitoredEntity& MonitoredEntity::operator=(const MonitoredEntity& other) {
    if (this != &other) {
        _name = other._name;
        _address = other._address;
        _icon = other._icon;
        _isOnline = other._isOnline;
        _notification = other._notification;
        _disabled = other._disabled;
        _lastChanged = other._lastChanged;
        _failCount = other._failCount;
    }
    return *this;
}
