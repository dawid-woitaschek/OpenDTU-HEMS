// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <WiFiClientSecure.h>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
inline std::string retryHeader;
struct FakeString {
    std::string text = retryHeader.empty() ? std::to_string(retryAfter) : retryHeader;
    char const* c_str() const { return text.c_str(); }
};
class HTTPClient {
    WiFiClient* client = nullptr;
public:
    HTTPClient() { failAllocation(2); ++activeHttp; }
    ~HTTPClient() { --activeHttp; }
    static std::string errorToString(int code) { return code == -1 ? "connection refused" : code == -11 ? "read Timeout" : "test HTTP error"; }
    bool begin(WiFiClient& c, char const* url) { failAllocation(3); client=&c; transportUrl=url; return beginAvailable; }
    void useHTTP10(bool) {}
    void setReuse(bool) {}
    void setFollowRedirects(int p) { redirectPolicy=p; }
    void setConnectTimeout(unsigned) {}
    void setTimeout(unsigned) {}
    void setUserAgent(char const*) {}
    void collectHeaders(char const**, size_t) { failAllocation(4); }
    int GET() {
        transportNow += getDelay;
        if (arbitraryFault) { throw std::runtime_error("must propagate"); }
        auto host = transportUrl.find("forecast.solar") != std::string::npos ? "api.forecast.solar" : "api.energy-charts.info";
        if (!client->connect(host, 443, 5000)) { return -1; }
        return transportCode;
    }
    FakeString header(char const*) { return {}; }
    int getSize() { return contentLength; }
    WiFiClient* getStreamPtr() { return client; }
    void end() {}
};
