#ifndef ELOG_MOCK_H
#define ELOG_MOCK_H

class MockLogger {
public:
    void info(int, const char*, ...) {}
    void error(int, const char*, ...) {}
    void warning(int, const char*, ...) {}
    void debug(int, const char*, ...) {}
    void critical(int, const char*, ...) {}
};

inline MockLogger Logger;

#endif // ELOG_MOCK_H
