#ifndef _PSRAM_ALLOCATOR_H_
#define _PSRAM_ALLOCATOR_H_

#include <ArduinoJson.h>

#ifdef UNIT_TEST
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
inline void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void heap_caps_free(void* ptr) { free(ptr); }
inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t) { return realloc(ptr, size); }
#endif

class PSRAMAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }
    void deallocate(void* ptr) override {
        heap_caps_free(ptr);
    }
    void* reallocate(void* ptr, size_t new_size) override {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    }
};

#endif // _PSRAM_ALLOCATOR_H_

