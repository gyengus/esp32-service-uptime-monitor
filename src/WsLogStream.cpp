#include "WsLogStream.h"
#include "WsService.h"
#include <cstring>

WsLogStream::WsLogStream() : _pos(0), _overflow(false) {
    memset(_buffer, 0, sizeof(_buffer));
}

void WsLogStream::clearBuffer() {
    _pos = 0;
    _overflow = false;
    _buffer[0] = '\0';
}

size_t WsLogStream::write(uint8_t c) {
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
        return 1;
    }

    if (c == '\r') {
        return 1;
    }

    if (_overflow) {
        // Discard rest of overflowing line until \n
        return 1;
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

    return 1;
}

size_t WsLogStream::write(const uint8_t *buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    for (size_t i = 0; i < size; ++i) {
        write(buffer[i]);
    }
    return size;
}

WsLogStream wsLogStream;
