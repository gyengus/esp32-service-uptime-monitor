#ifndef _API_H_
#define _API_H_

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class Api {
public:
    Api(AsyncWebServer& server);
    void registerEndpoints();
private:
    AsyncWebServer &server;
    void registerConfigEndpoints();
    void registerCommonEndpoints();
    void registerSystemInformationEndpoints();
    void registerServicesEndpoints();
    String cleanHostname(String &input);
};

#endif // _API_H_
