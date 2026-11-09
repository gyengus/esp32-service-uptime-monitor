#ifndef _MAIN_H_
#define _MAIN_H_

#include "devices.h"

void configurationMode();
void initNormalMode();
void commonWebServerSettings();
void setupOTA();
void onSntpSyncSchedule();
void errorMode();
void IRAM_ATTR handleButtonInterrupt();
void monitoringTaskLoop(void * pvParameters);
#ifdef HAS_RGB_LED
void monitoringLEDTask(void * pvParameters);
void apModeLEDTask(void * pvParameters);
#endif

#endif // _MAIN_H_
