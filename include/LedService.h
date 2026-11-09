#ifndef _LED_SERVICE_H_
#define _LED_SERVICE_H_

#include <Arduino.h>

class LedService {
public:
    LedService(const LedService&) = delete;
    LedService& operator=(const LedService&) = delete;
    static LedService& getInstance();

    void init();
    void setRgb(uint8_t r, uint8_t g, uint8_t b);
    void startTest(uint8_t brightnessPercent = 50, uint32_t durationMs = 3000);
    bool isTestActive() const;
    uint8_t getTestBrightness() const { return _testBrightness; }

    void updateMonitoringLed(uint32_t offlineCount);
    void updateApModeLed();
    void updateErrorLed();

    uint8_t getLastR() const { return _lastR; }
    uint8_t getLastG() const { return _lastG; }
    uint8_t getLastB() const { return _lastB; }

private:
    LedService() : _testEndMillis(0), _testBrightness(50), _lastR(0), _lastG(0), _lastB(0) {}
    unsigned long _testEndMillis;
    uint8_t _testBrightness;
    uint8_t _lastR;
    uint8_t _lastG;
    uint8_t _lastB;
};

extern LedService& ledService;

#endif // _LED_SERVICE_H_
