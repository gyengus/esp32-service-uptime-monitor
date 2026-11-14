#include "WebArchiveExtractor.h"
#include <Elog.h>
#include "Config.h"
#include "EmbeddedWebArchive.h"

#ifndef UNIT_TEST
#include <unzipLIB.h>
#include "Storage.h"

static File s_zipFile;

static void* zipOpenFileCallback(const char *filename, int32_t *size) {
    s_zipFile = storage.getFS().open(filename, FILE_READ);
    if (!s_zipFile) {
        return nullptr;
    }
    *size = s_zipFile.size();
    return (void *)&s_zipFile;
}

static void zipCloseFileCallback(void *p) {
    ZIPFILE *pzf = (ZIPFILE *)p;
    File *f = (File *)pzf->fHandle;
    if (f && *f) {
        f->close();
    }
}

static int32_t zipReadFileCallback(void *p, uint8_t *buffer, int32_t length) {
    ZIPFILE *pzf = (ZIPFILE *)p;
    File *f = (File *)pzf->fHandle;
    if (!f || !(*f)) return 0;
    return f->read(buffer, length);
}

static int32_t zipSeekFileCallback(void *p, int32_t position, int iType) {
    ZIPFILE *pzf = (ZIPFILE *)p;
    File *f = (File *)pzf->fHandle;
    if (!f || !(*f)) return 0;
    if (iType == SEEK_SET) {
        return f->seek(position);
    } else if (iType == SEEK_END) {
        return f->seek(position + pzf->iSize);
    } else { // SEEK_CUR
        long curPos = f->position();
        return f->seek(curPos + position);
    }
}

bool WebArchiveExtractor::ensureDirExists(const String& dirPath) {
    if (storage.getFS().exists(dirPath)) {
        return true;
    }
    int prevSlash = 0;
    while ((prevSlash = dirPath.indexOf('/', prevSlash + 1)) != -1) {
        String sub = dirPath.substring(0, prevSlash);
        if (!sub.isEmpty() && !storage.getFS().exists(sub)) {
            if (!storage.getFS().mkdir(sub)) {
                Logger.warning(MAINLOG, "Failed to create directory: %s", sub.c_str());
            }
        }
    }
    if (!storage.getFS().exists(dirPath)) {
        return storage.getFS().mkdir(dirPath);
    }
    return true;
}

static bool extractZipEntries(UNZIP* pZip, const String& targetDir) {
    WebArchiveExtractor::ensureDirExists(targetDir);

    int rc = pZip->gotoFirstFile();
    if (rc != UNZ_OK) {
        Logger.error(MAINLOG, "No files found in ZIP archive or invalid archive.");
        return false;
    }

    unz_file_info fi;
    char szFileName[256];
    size_t extractedCount = 0;
    const size_t CHUNK_SIZE = 4096;
    uint8_t* chunkBuf = (uint8_t*)malloc(CHUNK_SIZE);
    if (!chunkBuf) {
        Logger.error(MAINLOG, "Failed to allocate chunk buffer");
        return false;
    }

    while (rc == UNZ_OK) {
        if (pZip->getFileInfo(&fi, szFileName, sizeof(szFileName), NULL, 0, NULL, 0) == UNZ_OK) {
            String rawName = String(szFileName);

            // Path traversal protection
            if (rawName.indexOf("..") == -1) {
                if (rawName.startsWith("./")) rawName = rawName.substring(2);
                if (rawName.startsWith("web/")) rawName = rawName.substring(4);

                if (!rawName.isEmpty()) {
                    String targetPath = targetDir + "/" + rawName;

                    if (rawName.endsWith("/")) {
                        WebArchiveExtractor::ensureDirExists(targetPath);
                    } else {
                        int lastSlash = targetPath.lastIndexOf('/');
                        if (lastSlash > 0) {
                            WebArchiveExtractor::ensureDirExists(targetPath.substring(0, lastSlash));
                        }

                        if (pZip->openCurrentFile() == UNZ_OK) {
                            File outFile = storage.getFS().open(targetPath, FILE_WRITE);
                            if (outFile) {
                                int bytesRead = 0;
                                while ((bytesRead = pZip->readCurrentFile(chunkBuf, CHUNK_SIZE)) > 0) {
                                    outFile.write(chunkBuf, bytesRead);
#ifndef UNIT_TEST
                                    vTaskDelay(pdMS_TO_TICKS(1));
#endif
                                }
                                outFile.close();
#ifndef UNIT_TEST
                                vTaskDelay(pdMS_TO_TICKS(1));
#endif
                                extractedCount++;
                                Logger.debug(MAINLOG, "Extracted: %s (%u bytes)", rawName.c_str(), (unsigned int)fi.uncompressed_size);
                            } else {
                                Logger.error(MAINLOG, "Cannot open output file: %s", targetPath.c_str());
                            }
                            pZip->closeCurrentFile();
                        } else {
                            Logger.error(MAINLOG, "Failed to open current file in zip: %s", rawName.c_str());
                        }
                    }
                }
            } else {
                Logger.warning(MAINLOG, "Skipping entry with illegal path: %s", rawName.c_str());
            }
        }

#ifndef UNIT_TEST
        vTaskDelay(pdMS_TO_TICKS(1));
#endif
        rc = pZip->gotoNextFile();
    }

    free(chunkBuf);
    Logger.info(MAINLOG, "Extraction finished: %u files updated", extractedCount);
    return extractedCount > 0;
}

bool WebArchiveExtractor::extractZipArchive(const String& zipFilePath, const String& targetDir) {
    UNZIP* pZip = nullptr;
    if (psramFound()) {
        pZip = (UNZIP*)ps_malloc(sizeof(UNZIP));
    }
    if (!pZip) {
        pZip = (UNZIP*)malloc(sizeof(UNZIP));
    }
    if (!pZip) {
        Logger.error(MAINLOG, "Failed to allocate memory for UNZIP (41KB)");
        return false;
    }
    memset(pZip, 0, sizeof(UNZIP));

    int rc = pZip->openZIP(zipFilePath.c_str(), zipOpenFileCallback, zipCloseFileCallback, zipReadFileCallback, zipSeekFileCallback);
    if (rc != UNZ_OK) {
        Logger.error(MAINLOG, "Failed to open ZIP archive: %s, rc: %d", zipFilePath.c_str(), rc);
        free(pZip);
        return false;
    }

    bool success = extractZipEntries(pZip, targetDir);
    pZip->closeZIP();
    free(pZip);
    return success;
}

bool WebArchiveExtractor::extractZipFromBuffer(const uint8_t* buffer, size_t size, const String& targetDir) {
    if (!buffer || size == 0) {
        Logger.error(MAINLOG, "Invalid buffer passed to extractZipFromBuffer");
        return false;
    }

    UNZIP* pZip = nullptr;
    if (psramFound()) {
        pZip = (UNZIP*)ps_malloc(sizeof(UNZIP));
    }
    if (!pZip) {
        pZip = (UNZIP*)malloc(sizeof(UNZIP));
    }
    if (!pZip) {
        Logger.error(MAINLOG, "Failed to allocate memory for UNZIP (41KB)");
        return false;
    }
    memset(pZip, 0, sizeof(UNZIP));

    int rc = pZip->openZIP(const_cast<uint8_t*>(buffer), static_cast<int>(size));
    if (rc != UNZ_OK) {
        Logger.error(MAINLOG, "Failed to open in-memory ZIP archive, rc: %d", rc);
        free(pZip);
        return false;
    }

    bool success = extractZipEntries(pZip, targetDir);
    pZip->closeZIP();
    free(pZip);
    return success;
}

bool WebArchiveExtractor::extractEmbeddedArchive(const String& targetDir) {
    return extractZipFromBuffer(embedded_web_zip, embedded_web_zip_len, targetDir);
}

#else

bool WebArchiveExtractor::ensureDirExists(const String& dirPath) {
    (void)dirPath;
    return true;
}

bool WebArchiveExtractor::extractZipArchive(const String& zipFilePath, const String& targetDir) {
    (void)zipFilePath;
    (void)targetDir;
    return true;
}

bool WebArchiveExtractor::extractZipFromBuffer(const uint8_t* buffer, size_t size, const String& targetDir) {
    (void)buffer;
    (void)size;
    (void)targetDir;
    return true;
}

bool WebArchiveExtractor::extractEmbeddedArchive(const String& targetDir) {
    (void)targetDir;
    return true;
}

#endif
