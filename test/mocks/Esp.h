#ifndef MOCK_ESP_H
#define MOCK_ESP_H
#include <stdint.h>

class EspClass {
public:
    const char* getChipModel() { return "ESP32-S3"; }
    uint8_t getChipRevision() { return 2; }
    uint8_t getChipCores() { return 2; }
    uint32_t getCpuFreqMHz() { return 240; }
    uint64_t getEfuseMac() { return 0x112233445566ULL; }
    uint32_t getHeapSize() { return 400000; }
    uint32_t getFreeHeap() { return 300000; }
    uint32_t getMinFreeHeap() { return 250000; }
    uint32_t getMaxAllocHeap() { return 200000; }
    uint32_t getPsramSize() { return 8388608; }
    uint32_t getFreePsram() { return 8000000; }
    uint32_t getMinFreePsram() { return 7900000; }
    uint32_t getMaxAllocPsram() { return 4194304; }
    uint32_t getFlashChipSize() { return 16777216; }
    uint32_t getFlashChipSpeed() { return 80000000; }
    uint32_t getSketchSize() { return 1200000; }
    uint32_t getFreeSketchSpace() { return 2000000; }
    const char* getSdkVersion() { return "v5.1-mock"; }
    void restart() {}
};

inline EspClass ESP;

#endif // MOCK_ESP_H
