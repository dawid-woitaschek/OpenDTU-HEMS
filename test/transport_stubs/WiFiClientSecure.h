// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <Arduino.h>
#include <new>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
inline std::string transportBody, transportUrl, tlsHost;
inline bool caInstalled = false, dnsAvailable = true, bodyStalled = false;
inline bool beginAvailable = true;
inline int transportCode = 200, redirectPolicy = -1, contentLength = -1, tlsCode = 0;
inline int activeClients = 0, activeHttp = 0;
inline long retryAfter = 0;
// Faults in library-owned allocations, including constructor internals.
inline int allocationFault = 0;
inline bool arbitraryFault = false;
inline unsigned socketReads = 0, availableChecks = 0, connectionChecks = 0;
inline size_t maxReadSize = SIZE_MAX, extraAfterBody = 0;
inline uint32_t readDelay = 0, getDelay = 0, availableAt = 0;
inline void failAllocation(int point) { if (allocationFault == point) { throw std::bad_alloc(); } }

struct IPAddress {};
struct FakeWiFi { int hostByName(char const*, IPAddress&) { return dnsAvailable; } };
inline FakeWiFi WiFi;
class WiFiClient {
public:
    virtual ~WiFiClient() = default;
    virtual int connect(char const*, uint16_t, int32_t) { return 1; }
    size_t position = 0;
    int available() { ++availableChecks; return transportNow < availableAt ? 0 : transportBody.size() - position; }
    bool connected() { ++connectionChecks; return position < transportBody.size() || bodyStalled; }
    int read(uint8_t* data, size_t length) {
        failAllocation(6); ++socketReads; transportNow += readDelay;
        auto n = std::min(maxReadSize, std::min(length, transportBody.size() - position));
        memcpy(data,transportBody.data()+position,n); position += n; extraAfterBody = transportBody.size() - position; return n;
    }
};
class WiFiClientSecure : public WiFiClient {
protected:
    int32_t _timeout = 0;
public:
    using WiFiClient::connect;
    WiFiClientSecure() { failAllocation(1); ++activeClients; }
    ~WiFiClientSecure() { --activeClients; }
    int connect(IPAddress, uint16_t, char const* host, char const* ca, char const*, char const*) {
        failAllocation(5);
        tlsHost = host; caInstalled = ca && strstr(ca,"BEGIN CERTIFICATE"); return tlsCode >= 0;
    }
    int lastError(char* buffer, size_t length) { snprintf(buffer, length, "%s", tlsCode < 0 ? "test TLS error" : ""); return tlsCode; }
    void setCACert(char const* value) { caInstalled = value && strstr(value,"BEGIN CERTIFICATE"); }
    void setHandshakeTimeout(unsigned) {}
    void setTimeout(unsigned) {}
};
