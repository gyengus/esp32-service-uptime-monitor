#ifndef _MOCK_FS_H_
#define _MOCK_FS_H_

#include <Arduino.h>
#include <string>

#define FILE_READ "r"
#define FILE_WRITE "w"

class File : public Stream {
public:
    File() : _storage(nullptr), _open(false), _pos(0) {}
    File(const String& path, const String& mode, std::string* storageRef)
        : _path(path), _mode(mode), _storage(storageRef), _open(storageRef != nullptr), _pos(0) {
        if (_mode == "w" && _storage) {
            _storage->clear();
        }
    }

    operator bool() const { return _open; }
    size_t size() const { return (_open && _storage) ? _storage->size() : 0; }
    void close() { _open = false; }

    int available() override {
        if (!_open || !_storage) return 0;
        return _pos < _storage->size() ? (int)(_storage->size() - _pos) : 0;
    }

    int read() override {
        if (!available()) return -1;
        return (uint8_t)(*_storage)[_pos++];
    }

    int peek() override {
        if (!available()) return -1;
        return (uint8_t)(*_storage)[_pos];
    }

    void flush() override {}

    size_t write(uint8_t c) override {
        if (!_open || !_storage) return 0;
        _storage->push_back((char)c);
        _pos++;
        return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        if (!_open || !_storage || buffer == nullptr) return 0;
        _storage->append((const char*)buffer, size);
        _pos += size;
        return size;
    }

private:
    String _path;
    String _mode;
    std::string* _storage = nullptr;
    bool _open = false;
    size_t _pos = 0;
};

#endif // _MOCK_FS_H_
