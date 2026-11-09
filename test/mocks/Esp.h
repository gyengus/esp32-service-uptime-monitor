#ifndef MOCK_ESP_H
#define MOCK_ESP_H
#include <stdint.h>

class EspClass {
public:
    uint32_t getFreeHeap() { return 300000; }
    void restart() {}
};

inline EspClass ESP;

#endif // MOCK_ESP_H
