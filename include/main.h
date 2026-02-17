#ifndef _MAIN_H_
#define _MAIN_H_

void configurationMode();
void initNormalMode();
void commonWebServerSettings();
void onSntpSyncSchedule();
void errorMode();
void IRAM_ATTR handleButtonInterrupt();
void monitoringTaskLoop(void * pvParameters);
void monitoringLEDTask(void * pvParameters);
void apModeLEDTask(void * pvParameters);

#endif // _MAIN_H_
