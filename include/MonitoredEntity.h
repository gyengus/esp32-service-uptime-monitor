#ifndef _MONITORED_ENTITY_H_
#define _MONITORED_ENTITY_H_

#include <Arduino.h>
#include <ArduinoJson.h>

class MonitoredEntity {
public:
    MonitoredEntity(const String& name, const String& address, const String& icon, bool notify);
    MonitoredEntity();

    // Getters
    String getName() const;
    String getAddress() const;
    String getIcon() const;
    bool isOnline() const;
    bool getNotification() const;
    bool isDisabled() const;
    time_t getLastChanged() const;

    // Setters
    void setName(String &name);
    void setAddress(String &addr);
    void setIcon(String &icon);
    void setNotification(bool notify);
    void setDisabled(bool disabled);
    void setOnlineStatus(bool online);
    void setLastChanged(time_t lastChanged);

    String toJson() const;
    bool fromJson(const String& json);
    
    void fillJson(JsonObject& doc) const;
    bool fromJsonObject(const JsonObject& doc);
    MonitoredEntity& operator=(const MonitoredEntity& other);

    uint8_t getFailCount() const { return _failCount; }
    void resetFailCount() { _failCount = 0; }
    void incrementFailCount() { _failCount++; }
private:
    String _name;
    String _address;
    String _icon;
    time_t _lastChanged;
    bool _isOnline;
    bool _notification;
    bool _disabled;
    uint8_t _failCount;
};

#endif // _MONITORED_ENTITY_H_
