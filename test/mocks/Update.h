#ifndef MOCK_UPDATE_H
#define MOCK_UPDATE_H

#include <Arduino.h>
#include <vector>
#include <string>

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#define U_FLASH 0

class MockUpdateClass {
public:
    bool nextBeginResult = true;
    bool nextWriteResult = true;
    bool nextEndResult = true;
    String nextErrorString = "No Error";

    bool running = false;
    bool errorFlag = false;
    bool aborted = false;
    String expectedMd5;
    std::vector<uint8_t> writtenBytes;

    void resetMock() {
        nextBeginResult = true;
        nextWriteResult = true;
        nextEndResult = true;
        nextErrorString = "No Error";
        running = false;
        errorFlag = false;
        aborted = false;
        expectedMd5 = "";
        writtenBytes.clear();
    }

    bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int command = U_FLASH) {
        (void)size;
        (void)command;
        errorFlag = false;
        aborted = false;
        writtenBytes.clear();
        expectedMd5 = "";
        if (!nextBeginResult) {
            errorFlag = true;
            running = false;
            return false;
        }
        running = true;
        return true;
    }

    bool setMD5(const char* md5) {
        if (!md5) return false;
        expectedMd5 = md5;
        return true;
    }

    size_t write(uint8_t* data, size_t len) {
        if (!running || !nextWriteResult) {
            errorFlag = true;
            return 0;
        }
        if (data && len > 0) {
            writtenBytes.insert(writtenBytes.end(), data, data + len);
        }
        return len;
    }

    bool end(bool evenIfRemaining = false) {
        (void)evenIfRemaining;
        running = false;
        if (!nextEndResult) {
            errorFlag = true;
            return false;
        }
        return true;
    }

    void abort() {
        running = false;
        aborted = true;
        errorFlag = true;
    }

    bool isRunning() const {
        return running;
    }

    bool hasError() const {
        return errorFlag;
    }

    const char* errorString() const {
        return nextErrorString.c_str();
    }
};

inline MockUpdateClass Update;

#endif // MOCK_UPDATE_H
