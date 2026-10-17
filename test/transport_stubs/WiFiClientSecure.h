// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstring>
#include <string>
inline std::string transportBody, transportUrl;
inline bool caInstalled = false;
inline int transportCode = 200, redirectPolicy = -1, contentLength = -1;
inline long retryAfter = 0;
class WiFiClient {
public:
    virtual ~WiFiClient() = default;
    size_t position = 0;
    size_t readBytes(uint8_t* data, size_t length) {
        auto n = std::min(length, transportBody.size() - position); memcpy(data,transportBody.data()+position,n); position += n; return n;
    }
};
class WiFiClientSecure : public WiFiClient {
public:
    void setCACert(char const* value) { caInstalled = value && strstr(value,"BEGIN CERTIFICATE"); }
    void setHandshakeTimeout(unsigned) {}
    void setTimeout(unsigned) {}
};
