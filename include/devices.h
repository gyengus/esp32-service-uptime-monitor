#ifndef _DEVICES_H_
#define _DEVICES_H_

// Board selection based on build flags defined in platformio.ini

#if defined(BOARD_ESP32_S3_CAM)

    #define BOARD_NAME "ESP32-S3-CAM"
    #define BOOT_BTN_PIN 0

    // SD Card (SD_MMC 1-bit mode)
    #define HAS_SD_CARD
    #define SD_CMD_PIN 38
    #define SD_DATA_PIN 40
    #define SD_CLK_PIN 39

    // Status indicator LED
    #define HAS_STATUS_LED
    #define LED_PIN 2

    // Onboard addressable RGB LED (WS2812)
    #define HAS_RGB_LED
    #define RGB_LED_PIN 48

#elif defined(BOARD_ESP32_S3)

    #define BOARD_NAME "ESP32-S3"
    #define BOOT_BTN_PIN 0

    // Internal Flash via LittleFS (no SD card slot)
    #define USE_LITTLEFS

    // No status LED on this board
    // Onboard addressable RGB LED (WS2812)
    #define HAS_RGB_LED
    #define RGB_LED_PIN 48

#else

    // Default fallback configuration (ESP32-S3-CAM)
    #define BOARD_NAME "ESP32-S3-CAM"
    #define BOOT_BTN_PIN 0

    #define HAS_SD_CARD
    #define SD_CMD_PIN 38
    #define SD_DATA_PIN 40
    #define SD_CLK_PIN 39

    #define HAS_STATUS_LED
    #define LED_PIN 2

    #define HAS_RGB_LED
    #define RGB_LED_PIN 48

#endif

#endif // _DEVICES_H_
