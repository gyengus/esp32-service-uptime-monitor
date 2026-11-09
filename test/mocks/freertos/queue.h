#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <cstring>

struct MockQueue {
    size_t itemSize;
    size_t maxItems;
    std::deque<std::vector<uint8_t>> items;
};

inline QueueHandle_t xQueueCreate(size_t len, size_t itemSize) {
    auto q = new MockQueue();
    q->itemSize = itemSize;
    q->maxItems = len;
    return (QueueHandle_t)q;
}

inline bool xQueueSend(QueueHandle_t handle, const void* item, TickType_t wait) {
    if (!handle) return false;
    auto q = (MockQueue*)handle;
    if (q->items.size() >= q->maxItems) return false;
    std::vector<uint8_t> buf(q->itemSize);
    std::memcpy(buf.data(), item, q->itemSize);
    q->items.push_back(buf);
    return true;
}

inline bool xQueueReceive(QueueHandle_t handle, void* item, TickType_t wait) {
    if (!handle) return false;
    auto q = (MockQueue*)handle;
    if (q->items.empty()) return false;
    std::memcpy(item, q->items.front().data(), q->itemSize);
    q->items.pop_front();
    return true;
}

inline void vQueueDelete(QueueHandle_t handle) {
    if (handle) {
        delete (MockQueue*)handle;
    }
}

inline size_t uxQueueMessagesWaiting(QueueHandle_t handle) {
    if (!handle) return 0;
    return ((MockQueue*)handle)->items.size();
}
