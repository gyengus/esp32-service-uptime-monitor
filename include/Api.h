#ifndef _API_H_
#define _API_H_

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class Api {
public:
    Api(AsyncWebServer& server);
    void registerEndpoints();
    static String cleanHostname(const String &input);
private:
    AsyncWebServer &server;
    void registerConfigEndpoints();
    void registerCommonEndpoints();
    void registerSystemInformationEndpoints();
    void registerServicesEndpoints();
};

#endif // _API_H_
