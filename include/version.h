#ifndef __VERSION_H__
#define __VERSION_H__

#define FIRMWARE_VERSION "0.0.1-SNAPSHOT"

#ifndef COMMIT_HASH
    #define COMMIT_HASH "unknown"
#endif
#ifndef BUILD_TIMESTAMP
    #define BUILD_TIMESTAMP __DATE__ " " __TIME__
#endif

#endif // __VERSION_H__