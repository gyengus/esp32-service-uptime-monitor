#ifndef MOCK_WIFICLIENTSECURE_H
#define MOCK_WIFICLIENTSECURE_H
#include <Arduino.h>
#include <Client.h>

class WiFiClientSecure : public Client {
public:
    void setInsecure() {}
    int connect(IPAddress ip, uint16_t port) override { return 1; }
    int connect(const char *host, uint16_t port) override { return 1; }
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t *buf, size_t size) override { return size; }
    int available() override { return 0; }
    int read() override { return -1; }
    int read(uint8_t *buf, size_t size) override { return 0; }
    int peek() override { return -1; }
    void flush() override {}
    void stop() override {}
    uint8_t connected() override { return 1; }
    operator bool() override { return true; }
};

#endif // MOCK_WIFICLIENTSECURE_H
