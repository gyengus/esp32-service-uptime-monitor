#ifndef NOTIFICATION_SERVICE_H
#define NOTIFICATION_SERVICE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "Config.h"

class NotificationService {
public:
    NotificationService(const NotificationService&) = delete;
    NotificationService& operator=(const NotificationService&) = delete;
    static NotificationService& getInstance();
    bool send(const String& message, uint32_t color = DISCORD_COLOR_BLUE);
    bool send(const String& url, const String& message, uint32_t color = DISCORD_COLOR_BLUE);

private:
    NotificationService() {}
};

extern NotificationService& notificationService;

#endif // NOTIFICATION_SERVICE_H
