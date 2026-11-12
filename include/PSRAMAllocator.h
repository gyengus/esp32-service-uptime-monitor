#ifndef _PSRAM_ALLOCATOR_H_
#define _PSRAM_ALLOCATOR_H_

#include <ArduinoJson.h>

#ifdef UNIT_TEST
#include <cstdlib>
#else
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

class PSRAMAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override {
#ifndef UNIT_TEST
        if (psramFound()) {
            void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            if (ptr != nullptr) return ptr;
        }
#endif
        return malloc(size);
    }

    void deallocate(void* ptr) override {
        free(ptr);
    }

    void* reallocate(void* ptr, size_t new_size) override {
#ifndef UNIT_TEST
        if (psramFound()) {
            void* newPtr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
            if (newPtr != nullptr) return newPtr;
        }
#endif
        return realloc(ptr, new_size);
    }
};

#endif // _PSRAM_ALLOCATOR_H_

