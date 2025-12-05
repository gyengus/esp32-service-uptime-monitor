#ifndef _CONFIG_H_
#define _CONFIG_H_

typedef struct {
    const char* wifi_ssid;
    const char* wifi_password;
    const char* sntp_server;
    int timezone_offset_hours;
} config_t;

#define WIFI_AP_SSID "ESP32-Service-Uptime-Monitor"

#define DEFAULT_SNTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE_OFFSET_HOURS 1

#endif // _CONFIG_H_