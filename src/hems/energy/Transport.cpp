// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <RuntimeDiagnostics.h>
#include <hems/energy/Trust.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <ctime>

namespace Hems::Energy {
namespace {
// Arduino-ESP32 2.0.17 resolves DNS inside connect(), then passes the original
// hostname to TLS. Preserve that exact path while recording DNS separately.
// Do not use the IP-only connect overload: it loses hostname verification/SNI.
class DiagnosticClient final : public WiFiClientSecure {
public:
    explicit DiagnosticClient(NativeInfo& info) : _info(info) {}
    int connect(char const* host, uint16_t port, int32_t timeout) override {
        _timeout = timeout;
        IPAddress address;
        Diagnostics::workerPhase(Diagnostics::Phase::Dns);
        _info.dns = WiFi.hostByName(host, address) ? 1 : 0;
        if (!_info.dns) { _info.connected = memorySample(); return 0; }
        Diagnostics::workerPhase(Diagnostics::Phase::Connect);
        auto result = WiFiClientSecure::connect(address, port, host, ProviderRoots, nullptr, nullptr);
        // lastError() is the connect result, not a reliable read/write/socket errno.
        auto code = lastError(_info.tlsErrorText, sizeof(_info.tlsErrorText));
        if (code < 0) { _info.tlsError = code; }
        else { _info.tlsErrorText[0] = 0; }
        _info.connected = memorySample();
        if (result) { Diagnostics::workerPhase(Diagnostics::Phase::TlsHttp); }
        return result;
    }
private:
    NativeInfo& _info;
};
struct Reader {
    WiFiClient& stream;
    int length;
    size_t remaining = MaxPayload + 1, consumed = 0;
    bool expired = false, timedOut = false;
    uint32_t started = millis();
    int read() {
        if (!remaining || (length >= 0 && consumed == static_cast<size_t>(length))) { return -1; }
        if (millis() - started >= 15000) { expired = true; return -1; }
        if (!stream.available() && !stream.connected()) { return -1; }
        uint8_t c;
        auto count = stream.readBytes(&c, 1);
        if (millis() - started >= 15000) { expired = true; return -1; }
        if (count != 1) { timedOut = stream.connected(); return -1; }
        --remaining; ++consumed; return c;
    }
    size_t readBytes(char* data, size_t length) {
        size_t n = 0; for (; n < length; ++n) { auto c = read(); if (c < 0) { break; } data[n] = c; } return n;
    }
};
char const* transportError(int code, NativeInfo const& info)
{
    if (info.dns == 0) { return "provider_dns_failed"; }
    if (info.tlsError < -1) { return "provider_tls_error"; } // See the exact library code/text.
    switch (code) {
    case -1: return "provider_connect_failed"; // Library conflates TCP/socket timeout and some TLS errors.
    case -4: case -5: return "provider_connection_lost";
    case -8: return "insufficient_memory";
    case -11: return "provider_read_timeout";
    default: return "provider_transport_error";
    }
}
} // namespace
bool providerGet(char const* url, Source source, JsonDocument& doc, NativeInfo& info, char const*& error)
{
    info = {}; info.before = memorySample();
    error = "provider_begin_failed";
    char const* prefix = source == Source::EnergyCharts ? "https://api.energy-charts.info/" : "https://api.forecast.solar/";
    if (strncmp(url, prefix, strlen(prefix))) { error = "invalid_provider_origin"; return false; }
    auto client = std::unique_ptr<DiagnosticClient>(new (std::nothrow) DiagnosticClient(info));
    auto http = std::unique_ptr<HTTPClient>(new (std::nothrow) HTTPClient());
    if (!client || !http) { error = "insufficient_memory"; return false; }
    client->setCACert(ProviderRoots);
    client->setHandshakeTimeout(10);
    client->setTimeout(5); // WiFiClientSecure takes seconds, HTTPClient takes milliseconds.
    http->useHTTP10(true); http->setReuse(false);
    if (!http->begin(*client, url)) { return false; }
    http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http->setConnectTimeout(5000); http->setTimeout(5000); http->setUserAgent("OpenDTU-HEMS");
    char const* headers[] = { "Retry-After" }; http->collectHeaders(headers, 1);
    uint32_t started = millis();
    Diagnostics::workerPhase(Diagnostics::Phase::TlsHttp);
    int code = http->GET(); info.httpStatus = code; info.durationMs = millis() - started;
    if (code > 0) {
        info.retrySeconds = retryAfterSeconds(http->header("Retry-After").c_str(), time(nullptr));
        if (code == 429 || code == 503) {
            uint32_t floor = code == 503 ? 900 : source == Source::ForecastSolar ? 7200 : 60;
            info.retrySeconds = std::max(info.retrySeconds, floor);
        }
    }
    if (code != 200) {
        if (code < 0) { snprintf(info.httpError, sizeof(info.httpError), "%s", HTTPClient::errorToString(code).c_str()); }
        error = code == 429 ? "provider_rate_limited" : code > 0 ? "provider_http_error" : transportError(code, info);
        http->end(); return false;
    }
    if (http->getSize() > static_cast<int>(MaxPayload)) { error = "provider_payload_too_large"; http->end(); return false; }
    // Filter out unrelated fields before allocation. Neither cap reserves a payload buffer.
    JsonDocument filter;
    if (source == Source::EnergyCharts) {
        for (auto field : { "schema_version", "endpoint", "country", "bidding_zone", "timezone", "resolution",
                "interval_minutes", "unit", "generated_at", "available_from", "available_until", "license", "deprecated" }) { filter[field] = true; }
        filter["series"][0]["id"] = true; filter["series"][0]["unit"] = true;
        filter["data"][0]["timestamp"] = true; filter["data"][0]["values"]["day_ahead_price"] = true;
    }
    else {
        filter["result"]["watt_hours_period"] = true; filter["message"]["code"] = true;
        filter["message"]["type"] = true; filter["message"]["info"]["time_utc"] = true;
        filter["message"]["ratelimit"]["limit"] = true; filter["message"]["ratelimit"]["remaining"] = true;
        filter["message"]["ratelimit"]["period"] = true;
    }
    if (filter.overflowed()) { error = "insufficient_memory"; http->end(); return false; }
    Diagnostics::workerPhase(Diagnostics::Phase::Decode);
    Reader reader { *http->getStreamPtr(), http->getSize() };
    auto decoded = deserializeJson(doc, reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
    // Consume the entire HTTP body, bounded by Content-Length (if present), bytes and time.
    int c; bool trailing = false;
    while (!decoded && (c = reader.read()) >= 0) { if (c > 32) { trailing = true; break; } }
    info.payloadBytes = reader.consumed; info.decoded = memorySample(); info.durationMs = millis() - started;
    bool truncated = http->getSize() >= 0 && reader.consumed != static_cast<size_t>(http->getSize());
    bool ok = !decoded && !doc.overflowed() && reader.remaining && !trailing && !reader.expired && !reader.timedOut && !truncated;
    if (decoded == DeserializationError::NoMemory || doc.overflowed()) { error = "insufficient_memory"; }
    else if (reader.expired || reader.timedOut) { error = "provider_body_timeout"; }
    else if (truncated) { error = "provider_connection_lost"; }
    else if (!ok) { error = "malformed_or_oversized_json"; }
    else { error = nullptr; }
    http->end(); return ok;
}
} // namespace Hems::Energy
