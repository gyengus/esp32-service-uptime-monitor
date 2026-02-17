#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#include <Ticker.h>
#include "driver/temperature_sensor.h"

extern volatile bool isConfigFunctionsEnabled;
extern bool isAPMode;
extern volatile bool shouldRestart;
extern TaskHandle_t MonitoringTaskHandle;
extern temperature_sensor_handle_t temp_sensor;
extern uint8_t offlineEntityCount;

#endif // _GLOBALS_H
