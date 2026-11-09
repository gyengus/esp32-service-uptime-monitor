#include <unity.h>
#include <ArduinoFake.h>
#include "HostnameUtils.h"
#include "../mocks/MockGlobals.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_clean_hostname_lowercase_valid(void) {
    String res = HostnameUtils::cleanHostname("my-device-1");
    TEST_ASSERT_EQUAL_STRING("my-device-1", res.c_str());
}

void test_clean_hostname_uppercase_to_lower(void) {
    String res = HostnameUtils::cleanHostname("MyDevice123");
    TEST_ASSERT_EQUAL_STRING("mydevice123", res.c_str());
}

void test_clean_hostname_hungarian_accents_lowercase(void) {
    String res = HostnameUtils::cleanHostname("árvíztűrő-tükörfúrógép");
    TEST_ASSERT_EQUAL_STRING("arvizturo-tukorfurogep", res.c_str());
}

void test_clean_hostname_hungarian_accents_uppercase(void) {
    String res = HostnameUtils::cleanHostname("ÁRVÍZTŰRŐ-TÜKÖRFÚRÓGÉP");
    TEST_ASSERT_EQUAL_STRING("arvizturo-tukorfurogep", res.c_str());
}

void test_clean_hostname_spaces_and_slashes(void) {
    String res = HostnameUtils::cleanHostname("my device / room . 1");
    TEST_ASSERT_EQUAL_STRING("my_device_room_1", res.c_str());
}

void test_clean_hostname_collapse_consecutive_separators(void) {
    String res = HostnameUtils::cleanHostname("my    device");
    TEST_ASSERT_EQUAL_STRING("my_device", res.c_str());
}

void test_clean_hostname_trim_leading_trailing_delimiters(void) {
    String res = HostnameUtils::cleanHostname("---___my-host___---");
    TEST_ASSERT_EQUAL_STRING("my-host", res.c_str());
}

void test_clean_hostname_special_chars_stripped(void) {
    String res = HostnameUtils::cleanHostname("server!@#$%^&*()+={}[]");
    TEST_ASSERT_EQUAL_STRING("server", res.c_str());
}

void test_clean_hostname_empty_fallback(void) {
    String res = HostnameUtils::cleanHostname("!@#$%^&*()");
    TEST_ASSERT_EQUAL_STRING("esp32_service_uptime_monitor", res.c_str());
}

void test_clean_hostname_long_string_truncated_to_63(void) {
    String longName = "this-is-a-very-long-hostname-that-exceeds-the-sixty-three-character-limit-for-a-dns-label";
    String res = HostnameUtils::cleanHostname(longName);
    TEST_ASSERT_LESS_OR_EQUAL_INT(63, res.length());
    TEST_ASSERT_FALSE(res.endsWith("-"));
    TEST_ASSERT_FALSE(res.endsWith("_"));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_clean_hostname_lowercase_valid);
    RUN_TEST(test_clean_hostname_uppercase_to_lower);
    RUN_TEST(test_clean_hostname_hungarian_accents_lowercase);
    RUN_TEST(test_clean_hostname_hungarian_accents_uppercase);
    RUN_TEST(test_clean_hostname_spaces_and_slashes);
    RUN_TEST(test_clean_hostname_collapse_consecutive_separators);
    RUN_TEST(test_clean_hostname_trim_leading_trailing_delimiters);
    RUN_TEST(test_clean_hostname_special_chars_stripped);
    RUN_TEST(test_clean_hostname_empty_fallback);
    RUN_TEST(test_clean_hostname_long_string_truncated_to_63);
    return UNITY_END();
}
