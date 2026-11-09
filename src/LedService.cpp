#include "LedService.h"
#include "Config.h"

LedService& LedService::getInstance() {
    static LedService instance;
    return instance;
}

LedService& ledService = LedService::getInstance();

void LedService::init() {
    setRgb(0, 0, 0);
}

void LedService::setRgb(uint8_t r, uint8_t g, uint8_t b) {
    _lastR = r;
    _lastG = g;
    _lastB = b;
#ifndef UNIT_TEST
    if (RGB_LED_PIN >= 0) {
        rgbLedWrite(RGB_LED_PIN, r, g, b);
    }
#endif
}

void LedService::startTest(uint8_t brightnessPercent, uint32_t durationMs) {
    if (brightnessPercent > 100) brightnessPercent = 100;
    _testBrightness = brightnessPercent;
    _testEndMillis = millis() + durationMs;
    uint8_t val = (uint8_t)((255ULL * _testBrightness) / 100);
    // Unused color: Cyan (Red = 0, Green = val, Blue = val)
    setRgb(0, val, val);
}

bool LedService::isTestActive() const {
    if (_testEndMillis == 0) return false;
    long remaining = (long)(_testEndMillis - millis());
    return remaining > 0;
}

void LedService::updateMonitoringLed(uint32_t offlineCount) {
    if (isTestActive()) {
        uint8_t val = (uint8_t)((255ULL * _testBrightness) / 100);
        setRgb(0, val, val);
        return;
    }

    if (offlineCount > 0) {
        const int pulseSpeed = 1500; // ms
        uint8_t configuredBrightness = config.getConfig().ledBrightness;
        uint8_t maxBrightness = (uint8_t)((255ULL * configuredBrightness) / 100);

        if (maxBrightness == 0) {
            setRgb(0, 0, 0);
            return;
        }

        float progress = (float)(millis() % pulseSpeed) / pulseSpeed;
        float brightnessFactor = (sin(progress * 2.0 * PI - PI / 2) + 1.0) / 2.0;
        uint8_t currentBrightness = (uint8_t)(brightnessFactor * maxBrightness);
        setRgb(currentBrightness, 0, currentBrightness / 3);
    } else {
        setRgb(0, 0, 0);
    }
}

void LedService::updateApModeLed() {
    if (isTestActive()) {
        uint8_t val = (uint8_t)((255ULL * _testBrightness) / 100);
        setRgb(0, val, val);
        return;
    }

    const int pulseSpeed = 2000; // ms
    uint8_t configuredBrightness = config.getConfig().ledBrightness;
    uint8_t maxBrightness = (uint8_t)((255ULL * configuredBrightness) / 100);

    if (maxBrightness == 0) {
        setRgb(0, 0, 0);
        return;
    }

    float progress = (float)(millis() % pulseSpeed) / pulseSpeed;
    float brightnessFactor = (sin(progress * 2.0 * PI - PI / 2) + 1.0) / 2.0;
    uint8_t currentBrightness = (uint8_t)(brightnessFactor * maxBrightness);
    setRgb(currentBrightness, currentBrightness / 2, 0);
}

void LedService::updateErrorLed() {
    const int pulseSpeed = 4000; // ms
    uint8_t configuredBrightness = config.getConfig().ledBrightness;
    uint8_t maxBrightness = (uint8_t)((255ULL * configuredBrightness) / 100);

    if (maxBrightness == 0) {
        setRgb(0, 0, 0);
        return;
    }

    float progress = (float)(millis() % pulseSpeed) / pulseSpeed;
    float brightnessFactor = (sin(progress * 2.0 * PI - PI / 2) + 1.0) / 2.0;
    uint8_t currentBrightness = (uint8_t)(brightnessFactor * maxBrightness);
    setRgb(currentBrightness, 0, 0);
}
