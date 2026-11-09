#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "globals.h"

volatile bool isConfigFunctionsEnabled = false;
bool isAPMode = false;
volatile bool shouldRestart = false;
TaskHandle_t MonitoringTaskHandle = nullptr;
temperature_sensor_handle_t temp_sensor = nullptr;
