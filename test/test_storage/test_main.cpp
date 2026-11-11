#include <unity.h>
#include <Arduino.h>
#include "Storage.h"
#include "WebArchiveExtractor.h"
#include "../mocks/MockGlobals.cpp"

void setUp(void) {
    storage.clear();
}

void tearDown(void) {
    storage.clear();
}

void test_storage_init_mount_unmount_available(void) {
    TEST_ASSERT_TRUE(storage.init());
    TEST_ASSERT_TRUE(storage.mount());
    TEST_ASSERT_TRUE(storage.isAvailable());
    TEST_ASSERT_TRUE(storage.unmount());
}

void test_storage_write_and_read_file(void) {
    const String path = "/test.txt";
    const String content = "Hello ESP32 Uptime Monitor!";

    TEST_ASSERT_FALSE(storage.fileExists(path));
    TEST_ASSERT_TRUE(storage.writeFile(path, content));
    TEST_ASSERT_TRUE(storage.fileExists(path));

    String readContent = storage.readFile(path);
    TEST_ASSERT_EQUAL_STRING(content.c_str(), readContent.c_str());
}

void test_storage_read_nonexistent_returns_empty(void) {
    String readContent = storage.readFile("/does_not_exist.json");
    TEST_ASSERT_EQUAL_STRING("", readContent.c_str());
}

void test_storage_file_exists(void) {
    TEST_ASSERT_FALSE(storage.fileExists("/fileA.txt"));
    storage.writeFile("/fileA.txt", "abc");
    TEST_ASSERT_TRUE(storage.fileExists("/fileA.txt"));
}

void test_storage_delete_file(void) {
    const String path = "/to_delete.txt";
    storage.writeFile(path, "delete me");
    TEST_ASSERT_TRUE(storage.fileExists(path));

    TEST_ASSERT_TRUE(storage.deleteFile(path));
    TEST_ASSERT_FALSE(storage.fileExists(path));

    // Deleting again returns false
    TEST_ASSERT_FALSE(storage.deleteFile(path));
}

void test_storage_overwrite_file(void) {
    const String path = "/overwrite.txt";
    storage.writeFile(path, "initial");
    TEST_ASSERT_EQUAL_STRING("initial", storage.readFile(path).c_str());

    storage.writeFile(path, "updated content");
    TEST_ASSERT_EQUAL_STRING("updated content", storage.readFile(path).c_str());
}

void test_storage_get_file_read_stream(void) {
    const String path = "/stream_read.txt";
    storage.writeFile(path, "ABCDEF");

    File file = storage.getFile(path, "r");
    TEST_ASSERT_TRUE((bool)file);
    TEST_ASSERT_EQUAL_UINT(6, file.size());
    TEST_ASSERT_EQUAL_INT(6, file.available());

    // peek does not consume character
    TEST_ASSERT_EQUAL('A', (char)file.peek());
    TEST_ASSERT_EQUAL_INT(6, file.available());

    // read consumes
    TEST_ASSERT_EQUAL('A', (char)file.read());
    TEST_ASSERT_EQUAL('B', (char)file.read());
    TEST_ASSERT_EQUAL_INT(4, file.available());

    file.close();
    TEST_ASSERT_FALSE((bool)file);
}

void test_storage_get_file_write_stream(void) {
    const String path = "/stream_write.txt";

    File file = storage.getFile(path, "w");
    TEST_ASSERT_TRUE((bool)file);

    file.write((uint8_t)'X');
    const char* chunk = "YZ123";
    file.write((const uint8_t*)chunk, strlen(chunk));
    file.close();

    TEST_ASSERT_EQUAL_STRING("XYZ123", storage.readFile(path).c_str());
}

void test_storage_get_file_nonexistent_read_returns_false(void) {
    File file = storage.getFile("/nonexistent.txt", "r");
    TEST_ASSERT_FALSE((bool)file);
}

void test_storage_clear(void) {
    storage.writeFile("/f1.txt", "1");
    storage.writeFile("/f2.txt", "2");
    TEST_ASSERT_TRUE(storage.fileExists("/f1.txt"));
    TEST_ASSERT_TRUE(storage.fileExists("/f2.txt"));

    storage.clear();
    TEST_ASSERT_FALSE(storage.fileExists("/f1.txt"));
    TEST_ASSERT_FALSE(storage.fileExists("/f2.txt"));
}

void test_web_archive_extractor_embedded(void) {
    TEST_ASSERT_TRUE(WebArchiveExtractor::extractEmbeddedArchive("/web"));
    TEST_ASSERT_TRUE(WebArchiveExtractor::extractZipArchive("/web.zip", "/web"));
    TEST_ASSERT_TRUE(WebArchiveExtractor::ensureDirExists("/web"));
}

void test_storage_get_stats(void) {
    storage.writeFile("/stats_test.txt", "1234567890");
    StorageStats stats = storage.getStats();
    TEST_ASSERT_EQUAL_STRING("MockFS", stats.backend.c_str());
    TEST_ASSERT_TRUE(stats.available);
    TEST_ASSERT_EQUAL_UINT64(10, stats.usedBytes);
    TEST_ASSERT_EQUAL_UINT64(stats.totalBytes - 10, stats.freeBytes);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_storage_init_mount_unmount_available);
    RUN_TEST(test_storage_write_and_read_file);
    RUN_TEST(test_storage_read_nonexistent_returns_empty);
    RUN_TEST(test_storage_file_exists);
    RUN_TEST(test_storage_delete_file);
    RUN_TEST(test_storage_overwrite_file);
    RUN_TEST(test_storage_get_file_read_stream);
    RUN_TEST(test_storage_get_file_write_stream);
    RUN_TEST(test_storage_get_file_nonexistent_read_returns_false);
    RUN_TEST(test_storage_clear);
    RUN_TEST(test_web_archive_extractor_embedded);
    RUN_TEST(test_storage_get_stats);
    return UNITY_END();
}
