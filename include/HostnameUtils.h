#ifndef _HOSTNAME_UTILS_H_
#define _HOSTNAME_UTILS_H_

#include <Arduino.h>

class HostnameUtils {
public:
    static String cleanHostname(const String &rawInput);
};

#endif // _HOSTNAME_UTILS_H_
