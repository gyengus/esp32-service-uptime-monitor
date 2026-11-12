#include "WsLogStream.h"
#include "WsService.h"
#include <cstring>

WsLogStream::WsLogStream() : _pos(0), _overflow(false), _mutex(xSemaphoreCreateMutex()) {
    memset(_buffer, 0, sizeof(_buffer));
}

void WsLogStream::clearBuffer() {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY)) {
        _pos = 0;
        _overflow = false;
        _buffer[0] = '\0';
        xSemaphoreGive(_mutex);
    }
}

void WsLogStream::writeInternal(uint8_t c) {
    if (c == '\n') {
        if (!_overflow && _pos > 0) {
            if (_pos > 0 && _buffer[_pos - 1] == '\r') {
                _buffer[_pos - 1] = '\0';
            } else {
                _buffer[_pos] = '\0';
            }
            if (_pos > 0) {
                wsService.queueLogLine(_buffer);
            }
        }
        _pos = 0;
        _overflow = false;
        return;
    }

    if (c == '\r') {
        return;
    }

    if (_overflow) {
        // Discard rest of overflowing line until \n
        return;
    }

    if (_pos < sizeof(_buffer) - 1) {
        _buffer[_pos++] = (char)c;
        _buffer[_pos] = '\0';
    } else {
        // Line exceeded buffer length: truncate with ellipsis and set overflow flag
        if (sizeof(_buffer) > 4) {
            _buffer[sizeof(_buffer) - 4] = '.';
            _buffer[sizeof(_buffer) - 3] = '.';
            _buffer[sizeof(_buffer) - 2] = '.';
            _buffer[sizeof(_buffer) - 1] = '\0';
        }
        wsService.queueLogLine(_buffer);
        _pos = 0;
        _overflow = true;
    }
}

size_t WsLogStream::write(uint8_t c) {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY)) {
        writeInternal(c);
        xSemaphoreGive(_mutex);
        return 1;
    }
    return 0;
}

size_t WsLogStream::write(const uint8_t *buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY)) {
        for (size_t i = 0; i < size; ++i) {
            writeInternal(buffer[i]);
        }
        xSemaphoreGive(_mutex);
        return size;
    }
    return 0;
}

WsLogStream wsLogStream;
