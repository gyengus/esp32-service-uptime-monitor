#pragma once
#include <cstdint>

typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY 0xFFFFFFFFUL
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)1; }
inline bool xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return true; }
inline bool xSemaphoreGive(SemaphoreHandle_t) { return true; }
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
