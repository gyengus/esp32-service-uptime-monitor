#include <Arduino.h>
#include "FS.h"
#include "freertos/FreeRTOS.h"
#include "globals.h"

namespace fs {
FS mockFS;
}

volatile bool isConfigFunctionsEnabled = false;
bool isAPMode = false;
volatile bool shouldRestart = false;
volatile bool isOtaUpdating = false;
TaskHandle_t MonitoringTaskHandle = nullptr;
temperature_sensor_handle_t temp_sensor = (temperature_sensor_handle_t)0x1;
