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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_led_service_init);
    RUN_TEST(test_led_service_start_test);
    RUN_TEST(test_led_service_monitoring_all_online_turns_led_off);
    RUN_TEST(test_led_service_monitoring_offline_pulses_magenta);
    RUN_TEST(test_led_service_zero_brightness_keeps_led_off);
    return UNITY_END();
}
