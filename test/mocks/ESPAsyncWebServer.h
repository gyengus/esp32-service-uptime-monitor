#ifndef MOCK_ESPASYNCWEBSERVER_H
#define MOCK_ESPASYNCWEBSERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <map>
#include <regex>
#include "Esp.h"
#include "FS.h"

#define HTTP_GET     0b00000001
#define HTTP_POST    0b00000010
#define HTTP_DELETE  0b00000100
#define HTTP_PUT     0b00001000

typedef uint8_t WebRequestMethodComposite;

class AsyncWebServerRequest;

class AsyncWebServerResponse {
protected:
    int _code = 200;
    String _contentType;
    String _body;
public:
    virtual ~AsyncWebServerResponse() = default;
    int getCode() const { return _code; }
    const String& getContentType() const { return _contentType; }
    const String& getBody() const { return _body; }
    void setCode(int c) { _code = c; }
};

class AsyncResponseStream : public AsyncWebServerResponse, public Print {
public:
    AsyncResponseStream(const char* contentType) {
        _contentType = contentType;
        _code = 200;
    }
    size_t write(uint8_t c) override {
        _body += (char)c;
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) _body += (char)buffer[i];
        return size;
    }
    size_t print(const String &s) {
        _body += s;
        return s.length();
    }
    size_t print(const char* s) {
        _body += s;
        return strlen(s);
    }
    size_t println(const String &s) {
        _body += s;
        _body += "\n";
        return s.length() + 1;
    }
};

class AsyncJsonResponse : public AsyncWebServerResponse {
    JsonDocument _doc;
    JsonVariant _root;
public:
    AsyncJsonResponse(bool isArray = false) {
        _contentType = "application/json";
        _code = 200;
        if (isArray) {
            _root = _doc.add<JsonArray>();
        } else {
            _root = _doc.add<JsonObject>();
        }
    }
    JsonVariant& getRoot() { return _root; }
    void setLength() {
        _body = "";
        serializeJson(_root, _body);
    }
};

class AsyncWebParameter {
    String _value;
public:
    AsyncWebParameter(const String& v) : _value(v) {}
    const String& value() const { return _value; }
};

class AsyncWebServerRequest {
public:
    void* _tempObject = nullptr;
    File _tempFile;
    int lastCode = 0;
    String lastContentType;
    String lastBody;
    std::vector<String> _pathArgs;
    std::map<String, String> _params;
    std::map<String, String> _headers;

    ~AsyncWebServerRequest() {
        if (_tempObject) {
            delete (String*)_tempObject;
            _tempObject = nullptr;
        }
    }

    bool hasHeader(const char* name) const {
        return _headers.find(String(name)) != _headers.end();
    }

    bool hasHeader(const String& name) const {
        return _headers.find(name) != _headers.end();
    }

    void setHeader(const String& name, const String& val) {
        _headers[name] = val;
    }

    void send(int code, const char* contentType = "text/plain", const String& content = "") {
        lastCode = code;
        lastContentType = contentType;
        lastBody = content;
    }

    void send(AsyncWebServerResponse *response) {
        if (response) {
            lastCode = response->getCode();
            lastContentType = response->getContentType();
            lastBody = response->getBody();
            delete response;
        }
    }

    AsyncResponseStream* beginResponseStream(const char* contentType) {
        return new AsyncResponseStream(contentType);
    }

    bool hasParam(const String& name, bool post = false) const {
        return _params.find(name) != _params.end();
    }

    AsyncWebParameter* getParam(const String& name, bool post = false) const {
        auto it = _params.find(name);
        if (it != _params.end()) {
            return new AsyncWebParameter(it->second);
        }
        return nullptr;
    }

    void setParam(const String& name, const String& val) {
        _params[name] = val;
    }

    String pathArg(size_t index) const {
        if (index < _pathArgs.size()) return _pathArgs[index];
        return "";
    }
};

typedef std::function<void(AsyncWebServerRequest *request)> ArRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)> ArUploadHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)> ArBodyHandlerFunction;

struct MockRoute {
    String uri;
    WebRequestMethodComposite method;
    ArRequestHandlerFunction onRequest;
    ArUploadHandlerFunction onUpload;
    ArBodyHandlerFunction onBody;
    bool isRegex;
};

class AsyncWebServer {
public:
    std::vector<MockRoute> routes;

    AsyncWebServer(uint16_t port = 80) {}

    void on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest,
            ArUploadHandlerFunction onUpload = NULL, ArBodyHandlerFunction onBody = NULL) {
        bool regex = (uri[0] == '^');
        routes.push_back({String(uri), method, onRequest, onUpload, onBody, regex});
    }

    bool dispatch(const String& path, WebRequestMethodComposite method, AsyncWebServerRequest* req, const String& body = "") {
        for (auto& route : routes) {
            if (!(route.method & method)) continue;

            bool matched = false;
            req->_pathArgs.clear();

            if (route.isRegex) {
                try {
                    std::regex re(route.uri.c_str());
                    std::smatch match;
                    std::string sPath = path.c_str();
                    if (std::regex_match(sPath, match, re)) {
                        matched = true;
                        for (size_t i = 1; i < match.size(); i++) {
                            req->_pathArgs.push_back(String(match[i].str().c_str()));
                        }
                    }
                } catch (...) {}
            } else {
                if (route.uri.equals(path)) {
                    matched = true;
                }
            }

            if (matched) {
                if (route.onBody && body.length() > 0) {
                    route.onBody(req, (uint8_t*)body.c_str(), body.length(), 0, body.length());
                } else if (route.onBody && body.length() == 0) {
                    uint8_t dummy = 0;
                    route.onBody(req, &dummy, 0, 0, 0);
                }
                if (route.onRequest) {
                    route.onRequest(req);
                }
                return true;
            }
        }
        return false;
    }

    bool dispatchUpload(const String& path, WebRequestMethodComposite method, AsyncWebServerRequest* req, const String& filename, const String& data) {
        for (auto& route : routes) {
            if (!(route.method & method)) continue;
            if (route.uri.equals(path) && route.onUpload) {
                route.onUpload(req, filename, 0, (uint8_t*)data.c_str(), data.length(), true);
                if (route.onRequest) {
                    route.onRequest(req);
                }
                return true;
            }
        }
        return false;
    }
};

#endif // MOCK_ESPASYNCWEBSERVER_H
