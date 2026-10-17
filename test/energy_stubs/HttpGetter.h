// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
struct HttpRequestConfig { char Url[256] {}; uint16_t Timeout = 0; };
inline std::string httpBody;
inline bool httpAvailable = true;
inline std::function<void()> duringHttp;
inline unsigned httpRequests = 0;
class Stream {
public:
    size_t position = 0;
    size_t readBytes(char* target, size_t length) {
        auto count = std::min(length, httpBody.size() - position);
        memcpy(target, httpBody.data() + position, count); position += count; return count;
    }
};
class HttpRequestResult {
public:
    operator bool() const { return httpAvailable; }
    Stream* getStream() { return &stream; }
private:
    Stream stream;
};
class HttpGetter {
public:
    explicit HttpGetter(HttpRequestConfig const&) {}
    bool init() { return true; }
    HttpRequestResult performGetRequest() {
        ++httpRequests;
        if (duringHttp) { auto callback = std::move(duringHttp); duringHttp = {}; callback(); }
        return {};
    }
};
