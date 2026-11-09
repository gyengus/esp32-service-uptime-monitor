#pragma once
#include <cstdint>

typedef int esp_err_t;
#define ESP_OK 0

typedef enum {
    WIFI_PS_NONE,
    WIFI_PS_MIN_MODEM,
    WIFI_PS_MAX_MODEM
} wifi_ps_type_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP  = 1
} wifi_interface_t;

typedef enum {
    WIFI_BW_HT20 = 1,
    WIFI_BW_HT40 = 2
} wifi_bandwidth_t;

typedef enum {
    WIFI_POWER_19_5dBm = 78
} wifi_power_t;

inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
inline esp_err_t esp_wifi_set_max_tx_power(int8_t) { return ESP_OK; }
inline esp_err_t esp_wifi_set_bandwidth(wifi_interface_t, wifi_bandwidth_t) { return ESP_OK; }
inline const char* esp_err_to_name(esp_err_t) { return "OK"; }
