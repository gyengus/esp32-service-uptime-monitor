#pragma once
#include <cstdint>

typedef void* temperature_sensor_handle_t;
typedef int esp_err_t;
#define ESP_OK 0

inline esp_err_t temperature_sensor_enable(temperature_sensor_handle_t) { return ESP_OK; }
inline esp_err_t temperature_sensor_disable(temperature_sensor_handle_t) { return ESP_OK; }
inline esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t, float *tsens_out) {
    if (tsens_out) *tsens_out = 42.0f;
    return ESP_OK;
}
