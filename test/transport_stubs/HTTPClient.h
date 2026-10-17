// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <WiFiClientSecure.h>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
struct FakeString { long toInt() const { return retryAfter; } };
class HTTPClient {
    WiFiClient* client = nullptr;
public:
    bool begin(WiFiClient& c, char const* url) { client=&c; transportUrl=url; return true; }
    void useHTTP10(bool) {}
    void setReuse(bool) {}
    void setFollowRedirects(int p) { redirectPolicy=p; }
    void setConnectTimeout(unsigned) {}
    void setTimeout(unsigned) {}
    void setUserAgent(char const*) {}
    void collectHeaders(char const**, size_t) {}
    int GET() { return transportCode; }
    FakeString header(char const*) { return {}; }
    int getSize() { return contentLength; }
    WiFiClient* getStreamPtr() { return client; }
    void end() {}
};
