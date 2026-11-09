#pragma once
#include <cstdint>

typedef void esp_netif_t;
typedef int esp_err_t;
#define ESP_OK 0

inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char*) {
    static int dummy = 0;
    return (esp_netif_t*)&dummy;
}

inline esp_err_t esp_netif_set_hostname(esp_netif_t*, const char*) {
    return ESP_OK;
}
