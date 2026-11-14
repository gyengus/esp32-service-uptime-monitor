#ifndef _ADDRESS_VALIDATOR_H_
#define _ADDRESS_VALIDATOR_H_

#include <Arduino.h>

// Limits mirror the fixed-size buffers of WsQueueItem (NUL terminator included).
#define MAX_ENTITY_NAME_LENGTH 63
#define MAX_ENTITY_ADDRESS_LENGTH 127
#define MAX_ENTITY_ICON_LENGTH 127

namespace AddressValidator {

// ip is in host byte order (a.b.c.d == a<<24 | b<<16 | c<<8 | d)
bool isForbiddenIPv4(uint32_t ip);
bool isForbiddenIPv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

bool isValidMonitoredUrl(const String& url);
bool isValidEntityName(const String& name);
bool isValidEntityIcon(const String& icon);

}  // namespace AddressValidator

#endif  // _ADDRESS_VALIDATOR_H_
