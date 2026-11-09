#ifndef _WEB_ARCHIVE_EXTRACTOR_H_
#define _WEB_ARCHIVE_EXTRACTOR_H_

#include <Arduino.h>

class WebArchiveExtractor {
public:
    static bool extractZipArchive(const String& zipFilePath, const String& targetDir);
    static bool ensureDirExists(const String& dirPath);
};

#endif // _WEB_ARCHIVE_EXTRACTOR_H_
