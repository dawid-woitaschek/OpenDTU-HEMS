// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Arduino.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <new>
inline std::string responseBytes, requestBytes, verifiedHost;
inline bool dnsWorks = true, socketWorks = true, writeWorks = true, stallBody = false, failClientAllocation = false;
inline int tlsResult = 0, activeClients = 0;
inline bool verifiedRoots = false;
struct IPAddress {};
struct TestWiFi { int hostByName(char const*, IPAddress&) { return dnsWorks; } };
inline TestWiFi WiFi;
class WiFiClientSecure {
protected:
    int32_t _timeout = 0;
    size_t position = 0;
public:
    WiFiClientSecure() { if (failClientAllocation) throw std::bad_alloc(); ++activeClients; requestBytes.clear(); }
    virtual ~WiFiClientSecure() { --activeClients; }
    virtual int connect(char const*, uint16_t, int32_t) { return 0; }
    int connect(IPAddress, uint16_t, char const* host, char const* ca, char const*, char const*) { verifiedHost = host; verifiedRoots = ca && strstr(ca, "BEGIN CERTIFICATE"); return socketWorks && tlsResult >= 0; }
    int lastError(char* text, size_t size) { if (size) text[0] = 0; return tlsResult; }
    void setCACert(char const* ca) { verifiedRoots = ca && strstr(ca, "BEGIN CERTIFICATE"); }
    void setHandshakeTimeout(unsigned) {}
    void setTimeout(unsigned) {}
    size_t write(uint8_t const* bytes, size_t length) { if (!writeWorks) return 0; requestBytes.append(reinterpret_cast<char const*>(bytes), length); return length; }
    int available() { return responseBytes.size() - position; }
    int read() { return position < responseBytes.size() ? static_cast<unsigned char>(responseBytes[position++]) : -1; }
    bool connected() { return position < responseBytes.size() || stallBody; }
    void stop() {}
};
