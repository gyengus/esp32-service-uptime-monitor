#ifndef _SYSTEM_INFO_H_
#define _SYSTEM_INFO_H_

#include <ArduinoJson.h>

class SystemInfo {
public:
    /** Populates lightweight live metrics shared across WebSocket broadcasts and /api/sysinfo. */
    static void fillBasicSysinfo(JsonObject& doc);

    /** Populates complete hardware, memory, storage, network, time, monitoring, and firmware diagnostics. */
    static void fillDetailedSysinfo(JsonObject& doc);
};

#endif // _SYSTEM_INFO_H_
