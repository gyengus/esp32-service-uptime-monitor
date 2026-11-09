#ifndef MOCK_HTTPCLIENT_H
#define MOCK_HTTPCLIENT_H
#include <Arduino.h>
#include <map>
#include <string>
#include "WiFiClientSecure.h"

#define HTTP_REQUEST_TIMEOUT 5000

class HTTPClient {
public:
    static int mockResponseCode;
    static String lastUrl;
    static String lastMethod;
    static String lastPayload;
    static std::map<String, String> lastHeaders;

    static void resetMock() {
        mockResponseCode = 200;
        lastUrl = "";
        lastMethod = "";
        lastPayload = "";
        lastHeaders.clear();
    }

    bool begin(const String& url) {
        lastUrl = url;
        return true;
    }
    bool begin(WiFiClientSecure&, const String& url) {
        lastUrl = url;
        return true;
    }
    void setTimeout(uint16_t) {}
    void setReuse(bool) {}
    void addHeader(const String& name, const String& value) {
        lastHeaders[name] = value;
    }
    int sendRequest(const char* type, const String& payload = "") {
        lastMethod = type;
        lastPayload = payload;
        return mockResponseCode;
    }
    int POST(const String& payload) {
        return sendRequest("POST", payload);
    }
    void end() {}
    String errorToString(int code) {
        return code > 0 ? "OK" : "Connection Refused";
    }
};

inline int HTTPClient::mockResponseCode = 200;
inline String HTTPClient::lastUrl = "";
inline String HTTPClient::lastMethod = "";
inline String HTTPClient::lastPayload = "";
inline std::map<String, String> HTTPClient::lastHeaders;

#endif // MOCK_HTTPCLIENT_H
