#ifndef WS_LOG_STREAM_H
#define WS_LOG_STREAM_H

#include <Arduino.h>

#ifdef UNIT_TEST
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

class WsLogStream : public Stream {
public:
    WsLogStream();
    virtual ~WsLogStream() = default;

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    void clearBuffer();

private:
    char _buffer[384];
    size_t _pos;
    bool _overflow;
    SemaphoreHandle_t _mutex;
    void writeInternal(uint8_t c);
};

extern WsLogStream wsLogStream;

#endif // WS_LOG_STREAM_H
