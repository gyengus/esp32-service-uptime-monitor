#include <unity.h>
#include <Arduino.h>
#include <ArduinoFake.h>
#include "LedService.h"
#include "Config.h"
#include "Storage.h"

using namespace fakeit;

#include "../mocks/MockGlobals.cpp"

void setUp(void) {
    storage.clear();
    When(Method(ArduinoFake(), delay)).AlwaysReturn();
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    ledService.startTest(0, 0);
    ledService.init();
}

void tearDown(void) {
    ArduinoFakeReset();
    storage.clear();
}

void test_led_service_init(void) {
    ledService.init();
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());
    TEST_ASSERT_FALSE(ledService.isTestActive());
}

void test_led_service_start_test(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    ledService.startTest(50, 3000);

    TEST_ASSERT_TRUE(ledService.isTestActive());
    TEST_ASSERT_EQUAL_UINT8(50, ledService.getTestBrightness());
    // Cyan color: R = 0, G = 127, B = 127
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(127, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(127, ledService.getLastB());

    // Before expiration (e.g. at 2000 ms)
    When(Method(ArduinoFake(), millis)).AlwaysReturn(2000);
    TEST_ASSERT_TRUE(ledService.isTestActive());

    // After expiration (at 4500 ms)
    When(Method(ArduinoFake(), millis)).AlwaysReturn(4500);
    TEST_ASSERT_FALSE(ledService.isTestActive());
}

void test_led_service_monitoring_all_online_turns_led_off(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(5000);
    ledService.updateMonitoringLed(0);
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());
}

void test_led_service_monitoring_offline_pulses_magenta(void) {
    config_t cfg = config.getConfig();
    cfg.ledBrightness = 50;
    config.saveConfig(cfg);

    When(Method(ArduinoFake(), millis)).AlwaysReturn(5000);
    ledService.updateMonitoringLed(1);
    // At millis=5000 with pulseSpeed=1500, sin calculation produces non-zero
    // Red > 0, Green = 0, Blue = Red / 3
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
}

void test_led_service_zero_brightness_keeps_led_off(void) {
    config_t cfg = config.getConfig();
    cfg.ledBrightness = 0;
    config.saveConfig(cfg);

    When(Method(ArduinoFake(), millis)).AlwaysReturn(5000);
    ledService.updateMonitoringLed(2);
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());
}

void test_led_service_start_test_clamped_brightness(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    ledService.startTest(150, 2000); // Exceeds 100% -> should clamp to 100%
    TEST_ASSERT_TRUE(ledService.isTestActive());
    TEST_ASSERT_EQUAL_UINT8(100, ledService.getTestBrightness());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(255, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(255, ledService.getLastB());
}

void test_led_service_monitoring_during_active_test(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    ledService.startTest(60, 5000);
    TEST_ASSERT_TRUE(ledService.isTestActive());

    // Calling updateMonitoringLed while test is active keeps test color
    ledService.updateMonitoringLed(0);
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((255ULL * 60) / 100), ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((255ULL * 60) / 100), ledService.getLastB());
    ledService.startTest(0, 0);
}

void test_led_service_ap_mode_led(void) {
    // 1. When test is active
    When(Method(ArduinoFake(), millis)).AlwaysReturn(1000);
    ledService.startTest(50, 4000);
    ledService.updateApModeLed();
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(127, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(127, ledService.getLastB());

    // 2. Normal AP mode pulsing (millis after test expires)
    When(Method(ArduinoFake(), millis)).AlwaysReturn(6000);
    config_t cfg = config.getConfig();
    cfg.ledBrightness = 50;
    config.saveConfig(cfg);
    ledService.updateApModeLed();
    // In AP mode, Green = Red / 2, Blue = 0
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());

    // 3. Zero brightness keeps AP LED off
    cfg.ledBrightness = 0;
    config.saveConfig(cfg);
    ledService.updateApModeLed();
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());
}

void test_led_service_error_led(void) {
    When(Method(ArduinoFake(), millis)).AlwaysReturn(3000);
    config_t cfg = config.getConfig();
    cfg.ledBrightness = 50;
    config.saveConfig(cfg);

    ledService.updateErrorLed();
    // In Error mode, Green = 0, Blue = 0
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());

    // Zero brightness keeps Error LED off
    cfg.ledBrightness = 0;
    config.saveConfig(cfg);
    ledService.updateErrorLed();
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastR());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastG());
    TEST_ASSERT_EQUAL_UINT8(0, ledService.getLastB());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_led_service_init);
    RUN_TEST(test_led_service_start_test);
    RUN_TEST(test_led_service_start_test_clamped_brightness);
    RUN_TEST(test_led_service_monitoring_during_active_test);
    RUN_TEST(test_led_service_monitoring_all_online_turns_led_off);
    RUN_TEST(test_led_service_monitoring_offline_pulses_magenta);
    RUN_TEST(test_led_service_zero_brightness_keeps_led_off);
    RUN_TEST(test_led_service_ap_mode_led);
    RUN_TEST(test_led_service_error_led);
    return UNITY_END();
}
