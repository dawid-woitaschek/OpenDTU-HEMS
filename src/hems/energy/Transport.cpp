// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <RuntimeDiagnostics.h>
#include <hems/energy/Trust.h>
#include <hems/profiles/Profile.h>
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
    explicit DiagnosticClient(NativeInfo& info, Diagnostics::HeapProvider provider) : _info(info), _provider(provider) {}
    int connect(char const* host, uint16_t port, int32_t timeout) override {
        _timeout = timeout;
        IPAddress address;
        Diagnostics::workerPhase(Diagnostics::Phase::Dns);
        _info.dns = WiFi.hostByName(host, address) ? 1 : 0;
        if (!_info.dns) { _info.connected = memorySample(); return 0; }
        Diagnostics::workerPhase(Diagnostics::Phase::Connect);
        Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::BeforeConnect, _provider);
        auto result = WiFiClientSecure::connect(address, port, host, ProviderRoots, nullptr, nullptr);
        // lastError() is the connect result, not a reliable read/write/socket errno.
        auto code = lastError(_info.tlsErrorText, sizeof(_info.tlsErrorText));
        if (code < 0) { _info.tlsError = code; }
        else { _info.tlsErrorText[0] = 0; }
        _info.connected = memorySample();
        Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Connected, _provider);
        if (result) { Diagnostics::workerPhase(Diagnostics::Phase::TlsHttp); }
        return result;
    }
private:
    NativeInfo& _info;
    Diagnostics::HeapProvider _provider;
};
// ArduinoJson calls read() for JSON syntax. Refill a small fixed window using
// only currently available bytes; never ask Stream::readBytes to wait for an
// entire window. The idle timeout and total GET+body deadline remain independent.
struct Reader {
    WiFiClient& stream;
    int length;
    uint32_t started;
    size_t remaining = MaxPayload + 1, consumed = 0;
    bool expired = false, timedOut = false;
    uint8_t buffer[256];
    size_t position = 0, buffered = 0;
    Reader(WiFiClient& input, int size, uint32_t start) : stream(input), length(size), started(start) {}
    bool refill() {
        if (position < buffered) { return true; }
        if (!remaining || (length >= 0 && consumed == static_cast<size_t>(length))) { return false; }
        uint32_t waiting = millis();
        for (;;) {
            auto now = millis();
            if (now - started >= 15000) { expired = true; return false; }
            int available = stream.available();
            if (available > 0) {
                auto count = std::min(sizeof(buffer), std::min(remaining, static_cast<size_t>(available)));
                if (length >= 0) { count = std::min(count, static_cast<size_t>(length) - consumed); }
                int read = stream.read(buffer, count);
                if (millis() - started >= 15000) { expired = true; return false; }
                if (read > 0) { position = 0; buffered = read; return true; }
            }
            if (!stream.connected()) { return false; }
            if (millis() - waiting >= 5000) { timedOut = true; return false; }
            delay(1);
        }
    }
    int read() {
        if (!refill()) { return -1; }
        --remaining; ++consumed;
        return buffer[position++];
    }
    size_t readBytes(char* data, size_t length) {
        size_t copied = 0;
        while (copied < length && refill()) {
            auto count = std::min(length - copied, buffered - position);
            memcpy(data + copied, buffer + position, count);
            position += count; consumed += count; remaining -= count; copied += count;
        }
        return copied;
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
static bool request(char const* url, Source source, JsonDocument& doc, NativeInfo& info, char const*& error)
{
    info = {}; info.before = memorySample();
    auto provider = source == Source::EnergyCharts ? Diagnostics::HeapProvider::EnergyCharts : Diagnostics::HeapProvider::ForecastSolar;
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::BeforeRequest, provider);
    error = "provider_begin_failed";
    char const* prefix = source == Source::EnergyCharts ? "https://api.energy-charts.info/" : "https://api.forecast.solar/";
    if (strncmp(url, prefix, strlen(prefix))) { error = "invalid_provider_origin"; return false; }
    auto client = std::unique_ptr<DiagnosticClient>(new (std::nothrow) DiagnosticClient(info, provider));
    auto http = std::unique_ptr<HTTPClient>(new (std::nothrow) HTTPClient());
    if (!client || !http) { error = "insufficient_memory"; return false; }
    client->setCACert(ProviderRoots);
    client->setHandshakeTimeout(10);
    client->setTimeout(5); // WiFiClientSecure takes seconds, HTTPClient takes milliseconds.
    http->useHTTP10(true); http->setReuse(false);
    if (!http->begin(*client, url)) { return false; }
    http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http->setConnectTimeout(5000); http->setTimeout(5000); http->setUserAgent("OpenDTU-HEMS");
    // Fresh HTTPClient: collectHeaders is called once. In Arduino 2.0.17 an
    // allocation failure here leaves its initially-null header pointer safe to destroy.
    char const* headers[] = { "Retry-After" }; http->collectHeaders(headers, 1);
    uint32_t started = millis();
    Diagnostics::workerPhase(Diagnostics::Phase::TlsHttp);
    int code = http->GET(); info.httpStatus = code; info.durationMs = millis() - started;
    if (code > 0) { Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Response, provider); }
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
    // Allow the larger native-host ArduinoJson pool too; this is a cap, not a reservation.
    Profiles::BoundedAllocator filterAllocator(12288, true);
    JsonDocument filter(&filterAllocator);
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
    Reader reader { *http->getStreamPtr(), http->getSize(), started };
    auto decoded = deserializeJson(doc, reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(6));
    // Consume the entire HTTP body, bounded by Content-Length (if present), bytes and time.
    int c; bool trailing = false;
    while (!decoded && (c = reader.read()) >= 0) { if (c > 32) { trailing = true; break; } }
    if (millis() - started >= 15000) { reader.expired = true; }
    info.payloadBytes = reader.consumed; info.decoded = memorySample(); info.durationMs = millis() - started;
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Parsed, provider);
    bool truncated = http->getSize() >= 0 && reader.consumed != static_cast<size_t>(http->getSize());
    bool ok = !decoded && !doc.overflowed() && reader.remaining && !trailing && !reader.expired && !reader.timedOut && !truncated;
    if (decoded == DeserializationError::NoMemory || doc.overflowed()) { error = "insufficient_memory"; }
    else if (reader.expired || reader.timedOut) { error = "provider_body_timeout"; }
    else if (truncated) { error = "provider_connection_lost"; }
    else if (!ok) { error = "malformed_or_oversized_json"; }
    else { error = nullptr; }
    http->end(); return ok;
}
bool providerGet(char const* url, Source source, JsonDocument& doc, NativeInfo& info, char const*& error)
{
    // nothrow covers only the outer operator new. Arduino constructors and
    // HTTPClient header storage also allocate; unwind request-local RAII on OOM.
    // Do not catch arbitrary exceptions, asserts, or allocator corruption.
    try {
        return request(url, source, doc, info, error);
    } catch (std::bad_alloc const&) {
        doc.clear();
        error = "insufficient_memory";
        return false;
    }
}
} // namespace Hems::Energy
