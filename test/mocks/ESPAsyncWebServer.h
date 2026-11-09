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

class AsyncWebHandler {
public:
    virtual ~AsyncWebHandler() = default;
};

#ifndef WS_CONNECTED
#define WS_CONNECTED 1
#endif

class AsyncWebSocketClient {
public:
    AsyncWebSocketClient(uint32_t id = 1) : _id(id) {}
    uint32_t id() const { return _id; }
    uint8_t status() const { return WS_CONNECTED; }
    bool text(const String& message) {
        lastMessage = message;
        sentMessages.push_back(message);
        return true;
    }
    String lastMessage;
    std::vector<String> sentMessages;
private:
    uint32_t _id;
};

enum AwsEventType {
    WS_EVT_CONNECT,
    WS_EVT_DISCONNECT,
    WS_EVT_DATA,
    WS_EVT_PONG,
    WS_EVT_ERROR
};

class AsyncWebSocket;
typedef std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)> AwsEventHandler;

class AsyncWebSocket : public AsyncWebHandler {
public:
    AsyncWebSocket(const char* url) : _url(url) {}
    ~AsyncWebSocket() {
        for (auto c : _clients) delete c;
        _clients.clear();
    }
    void onEvent(AwsEventHandler handler) { _handler = handler; }
    void textAll(const String& message) {
        lastBroadcast = message;
        allBroadcasts.push_back(message);
        for (auto c : _clients) {
            c->text(message);
        }
    }
    void cleanupClients() {}
    size_t count() const { return _clients.size(); }
    AsyncWebSocketClient* client(uint32_t id) {
        for (auto c : _clients) {
            if (c->id() == id) return c;
        }
        return nullptr;
    }
    void closeAll() {
        for (auto c : _clients) delete c;
        _clients.clear();
    }

    AsyncWebSocketClient* mockConnect(uint32_t id = 1) {
        auto c = new AsyncWebSocketClient(id);
        _clients.push_back(c);
        if (_handler) {
            _handler(this, c, WS_EVT_CONNECT, nullptr, nullptr, 0);
        }
        return c;
    }

    void mockDisconnect(AsyncWebSocketClient* c) {
        if (_handler) {
            _handler(this, c, WS_EVT_DISCONNECT, nullptr, nullptr, 0);
        }
        auto it = std::find(_clients.begin(), _clients.end(), c);
        if (it != _clients.end()) {
            _clients.erase(it);
            delete c;
        }
    }

    void mockData(AsyncWebSocketClient* c, const String& data) {
        if (_handler) {
            _handler(this, c, WS_EVT_DATA, nullptr, (uint8_t*)data.c_str(), data.length());
        }
    }

    String lastBroadcast;
    std::vector<String> allBroadcasts;
    AwsEventHandler _handler;
    std::vector<AsyncWebSocketClient*> _clients;
    String _url;
};

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
    std::vector<AsyncWebHandler*> handlers;

    AsyncWebServer(uint16_t port = 80) {}

    void addHandler(AsyncWebHandler* handler) {
        handlers.push_back(handler);
    }

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
