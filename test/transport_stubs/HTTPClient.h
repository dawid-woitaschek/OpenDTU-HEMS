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
    HTTPClient() { ++activeHttp; }
    ~HTTPClient() { --activeHttp; }
    static std::string errorToString(int code) { return code == -1 ? "connection refused" : code == -11 ? "read Timeout" : "test HTTP error"; }
    bool begin(WiFiClient& c, char const* url) { client=&c; transportUrl=url; return true; }
    void useHTTP10(bool) {}
    void setReuse(bool) {}
    void setFollowRedirects(int p) { redirectPolicy=p; }
    void setConnectTimeout(unsigned) {}
    void setTimeout(unsigned) {}
    void setUserAgent(char const*) {}
    void collectHeaders(char const**, size_t) {}
    int GET() {
        auto host = transportUrl.find("forecast.solar") != std::string::npos ? "api.forecast.solar" : "api.energy-charts.info";
        if (!client->connect(host, 443, 5000)) { return -1; }
        return transportCode;
    }
    FakeString header(char const*) { return {}; }
    int getSize() { return contentLength; }
    WiFiClient* getStreamPtr() { return client; }
    void end() {}
};
