// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
inline std::string transportBody, transportUrl, tlsHost;
inline bool caInstalled = false, dnsAvailable = true, bodyStalled = false;
inline int transportCode = 200, redirectPolicy = -1, contentLength = -1, tlsCode = 0;
inline int activeClients = 0, activeHttp = 0;
inline long retryAfter = 0;
struct IPAddress {};
struct FakeWiFi { int hostByName(char const*, IPAddress&) { return dnsAvailable; } };
inline FakeWiFi WiFi;
class WiFiClient {
public:
    virtual ~WiFiClient() = default;
    virtual int connect(char const*, uint16_t, int32_t) { return 1; }
    size_t position = 0;
    int available() { return transportBody.size() - position; }
    bool connected() { return position < transportBody.size() || bodyStalled; }
    size_t readBytes(uint8_t* data, size_t length) {
        auto n = std::min(length, transportBody.size() - position); memcpy(data,transportBody.data()+position,n); position += n; return n;
    }
};
class WiFiClientSecure : public WiFiClient {
protected:
    int32_t _timeout = 0;
public:
    using WiFiClient::connect;
    WiFiClientSecure() { ++activeClients; }
    ~WiFiClientSecure() { --activeClients; }
    int connect(IPAddress, uint16_t, char const* host, char const* ca, char const*, char const*) {
        tlsHost = host; caInstalled = ca && strstr(ca,"BEGIN CERTIFICATE"); return tlsCode >= 0;
    }
    int lastError(char* buffer, size_t length) { snprintf(buffer, length, "%s", tlsCode < 0 ? "test TLS error" : ""); return tlsCode; }
    void setCACert(char const* value) { caInstalled = value && strstr(value,"BEGIN CERTIFICATE"); }
    void setHandshakeTimeout(unsigned) {}
    void setTimeout(unsigned) {}
};
