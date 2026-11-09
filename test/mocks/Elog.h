#ifndef ELOG_MOCK_H
#define ELOG_MOCK_H

#include <Arduino.h>

#ifndef MAINLOG
#define MAINLOG 0
#endif

enum LogLevel {
    ELOG_LEVEL_ALWAYS = 0,
    ELOG_LEVEL_EMERGENCY = 1,
    ELOG_LEVEL_ALERT = 2,
    ELOG_LEVEL_CRITICAL = 3,
    ELOG_LEVEL_ERROR = 4,
    ELOG_LEVEL_WARNING = 5,
    ELOG_LEVEL_NOTICE = 6,
    ELOG_LEVEL_INFO = 7,
    ELOG_LEVEL_DEBUG = 8,
    ELOG_LEVEL_TRACE = 9,
    ELOG_LEVEL_VERBOSE = 10,
    ELOG_LEVEL_NOLOG = 11
};

class MockLogger {
public:
    uint8_t currentLevel = ELOG_LEVEL_DEBUG;
    void info(int, const char*, ...) {}
    void error(int, const char*, ...) {}
    void warning(int, const char*, ...) {}
    void debug(int, const char*, ...) {}
    void critical(int, const char*, ...) {}
    void registerSerial(uint8_t, uint8_t level, const char*, Stream& = Serial, uint8_t = 0) { currentLevel = level; }
    void setSerialLogLevel(uint8_t, uint8_t level, Stream& = Serial) { currentLevel = level; }
    uint8_t getSerialLogLevel(uint8_t, Stream& = Serial) { return currentLevel; }
};

inline MockLogger Logger;

#endif // ELOG_MOCK_H

