// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <hems/energy/Trust.h>
#include <hems/profiles/Profile.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace Hems::Energy {
namespace {
struct Reader {
    WiFiClient& stream;
    size_t remaining = MaxPayload + 1;
    bool expired = false;
    uint32_t started = millis();
    int read() {
        if (!remaining) { return -1; }
        if (millis() - started >= 15000) { expired = true; return -1; }
        uint8_t c;
        auto count = stream.readBytes(&c, 1);
        if (millis() - started >= 15000) { expired = true; return -1; }
        if (count != 1) { return -1; }
        --remaining; return c;
    }
    size_t readBytes(char* data, size_t length) {
        size_t n = 0; for (; n < length; ++n) { auto c = read(); if (c < 0) { break; } data[n] = c; } return n;
    }
};
} // namespace
bool providerGet(char const* url, Source source, JsonDocument& doc, NativeInfo& info, char const*& error)
{
    error = "verified_https_failed";
    char const* prefix = source == Source::EnergyCharts ? "https://api.energy-charts.info/" : "https://api.forecast.solar/";
    if (strncmp(url, prefix, strlen(prefix))) { error = "invalid_provider_origin"; return false; }
    auto client = std::unique_ptr<WiFiClientSecure>(new (std::nothrow) WiFiClientSecure());
    auto http = std::unique_ptr<HTTPClient>(new (std::nothrow) HTTPClient());
    if (!client || !http) { error = "insufficient_memory"; return false; }
    client->setCACert(ProviderRoots);
    client->setHandshakeTimeout(10);
    client->setTimeout(5000);
    http->useHTTP10(true); http->setReuse(false);
    // begin with the DNS URL: preserves hostname for TLS SNI and certificate SAN verification.
    if (!http->begin(*client, url)) { return false; }
    http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http->setConnectTimeout(5000); http->setTimeout(5000); http->setUserAgent("OpenDTU-HEMS");
    char const* headers[] = { "Retry-After" }; http->collectHeaders(headers, 1);
    int code = http->GET(); info.httpStatus = code;
    if (code == 429 || code == 503) {
        int32_t retry = http->header("Retry-After").toInt();
        info.retrySeconds = std::max(static_cast<uint32_t>(code == 429 ? 7200 : 900), static_cast<uint32_t>(std::max(static_cast<int32_t>(0), retry)));
    }
    if (code != 200) {
        error = code == 429 ? "provider_rate_limited" : code > 0 ? "provider_http_error" : "verified_https_failed";
        http->end(); return false;
    }
    if (http->getSize() > static_cast<int>(MaxPayload)) { error = "provider_payload_too_large"; http->end(); return false; }
    // Filter out unrelated provider fields before allocation (e.g. instantaneous watts, location text).
    JsonDocument filter;
    if (source == Source::EnergyCharts) { filter["unix_seconds"] = true; filter["price"] = true; filter["unit"] = true; }
    else {
        filter["result"]["watt_hours_period"] = true; filter["message"]["code"] = true;
        filter["message"]["type"] = true; filter["message"]["info"]["time_utc"] = true;
        filter["message"]["ratelimit"]["limit"] = true; filter["message"]["ratelimit"]["remaining"] = true;
        filter["message"]["ratelimit"]["period"] = true;
    }
    Reader reader { *http->getStreamPtr() };
    auto decoded = deserializeJson(doc, reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
    // Consume trailing bytes too: a valid JSON prefix cannot hide an oversized/invalid response.
    int c; bool trailing = false;
    while (!decoded && (c = reader.read()) >= 0) { if (c > 32) { trailing = true; break; } }
    bool ok = !decoded && !doc.overflowed() && reader.remaining && !trailing && !reader.expired
        && (http->getSize() < 0 || MaxPayload + 1 - reader.remaining == static_cast<size_t>(http->getSize()));
    if (!ok) { error = "malformed_or_oversized_json"; }
    else { error = nullptr; }
    http->end(); return ok;
}
} // namespace Hems::Energy
