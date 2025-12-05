#include <Arduino.h>
#include <Wire.h>

#include "config.h"

void setup() {
    #ifdef MONITOR_SPEED
        Serial.begin(MONITOR_SPEED);
    #else
        Serial.begin(115200);
    #endif //MONITOR_SPEED
    Serial.println("Starting up...");

}

void loop() {
    Serial.print(".");
    delay(2000);
}
