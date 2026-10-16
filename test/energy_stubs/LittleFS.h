// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
inline std::map<std::string, std::shared_ptr<std::string>> testFiles;
inline bool failCommit = false;
class TestFile {
public:
    std::shared_ptr<std::string> content;
    size_t position = 0;
    explicit operator bool() const { return bool(content); }
    size_t size() const { return content ? content->size() : 0; }
    int read() { return content && position < content->size() ? static_cast<unsigned char>((*content)[position++]) : -1; }
    size_t readBytes(char* buffer, size_t size) {
        auto count = std::min(size, content ? content->size() - position : 0);
        if (count) { memcpy(buffer, content->data() + position, count); position += count; } return count;
    }
    size_t write(uint8_t const* data, size_t size) {
        if (!content) { return 0; }
        content->append(reinterpret_cast<char const*>(data), size); return size;
    }
    size_t write(uint8_t value) { return write(&value, 1); }
    void flush() {}
    void close() { content.reset(); }
};
inline struct TestFS {
    TestFile open(char const* path, char const* mode) {
        if (*mode == 'w') { testFiles[path] = std::make_shared<std::string>(); }
        auto found = testFiles.find(path);
        return { found == testFiles.end() ? nullptr : found->second };
    }
    void remove(char const* path) { testFiles.erase(path); }
    bool rename(char const* from, char const* to) {
        if (failCommit || !testFiles.count(from)) { return false; }
        testFiles[to] = testFiles[from]; testFiles.erase(from); return true;
    }
} LittleFS;
