#ifndef NOTIFICATION_SERVICE_H
#define NOTIFICATION_SERVICE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class NotificationService {
public:
    NotificationService(const NotificationService&) = delete;
    NotificationService& operator=(const NotificationService&) = delete;
    static NotificationService& getInstance();
    bool send(const String& message);
    bool send(const String& url, const String& message);

private:
    NotificationService() {  };
    WiFiClientSecure* client;
    HTTPClient* http;
    void initContext();
};

extern NotificationService& notificationService;

#endif // NOTIFICATION_SERVICE_H
