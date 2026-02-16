#include "PingUtils.h"
#include <WiFi.h>

#include "Config.h"

bool nativePing(const String& address) {
    WiFiClient *client = new WiFiClient;
    client->setTimeout(HTTP_REQUEST_TIMEOUT);

    if (client->connect(address.c_str(), 80)) {
        client->stop();
        return true;
    }

    if (client->connect(address.c_str(), 443)) {
        client->stop();
        return true;
    }

    if (client->connect(address.c_str(), 22)) {
        client->stop();
        return true;
    }

    delete client;
    return false;
}
