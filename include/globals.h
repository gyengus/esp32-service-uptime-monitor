#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#ifdef UNIT_TEST
#include "freertos/FreeRTOS.h"
#endif

#include <Ticker.h>
#include "driver/temperature_sensor.h"

extern volatile bool isConfigFunctionsEnabled;
extern bool isAPMode;
extern volatile bool shouldRestart;
extern TaskHandle_t MonitoringTaskHandle;
extern temperature_sensor_handle_t temp_sensor;

#endif // _GLOBALS_H
