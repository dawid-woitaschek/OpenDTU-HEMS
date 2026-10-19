// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

namespace Hems::Energy {

constexpr size_t MaxIntervals = 256; // 64 hours at 15 minutes, including DST.
constexpr size_t MaxPayload = 40000;
constexpr size_t MaxJsonMemory = 49152;
constexpr uint32_t MinUtc = 1577836800; // Reject an unsynchronized clock (before 2020).
enum class Domain : uint8_t { Tariff, Forecast };

// Half-open UTC Unix-second intervals. Tariff values: millionths of currency/kWh.
// Forecast value: average W, so interval energy is value * (end-start) / 3600 Wh.
struct Interval {
    uint32_t start = 0, end = 0;
    int32_t value = 0, importPrice = 0;
    bool hasImportPrice = false;
};
struct Series {
    Interval intervals[MaxIntervals] {};
    uint32_t generatedAt = 0; // Provider response generation, not upstream market publication.
    bool sourceTimestampKnown = true;
    uint16_t count = 0;
    char source[49] {}, currency[4] {};
    Interval const* current(uint32_t utc) const;
    bool covers(uint32_t start, uint32_t end) const;
    // Exact integer watt-seconds; returns false across any missing interval.
    bool energyWattSeconds(uint32_t start, uint32_t end, int64_t& result) const;
};
enum class Source : uint8_t { Normalized, EnergyCharts, ForecastSolar };
constexpr size_t MaxPlanes = 6;
struct Plane {
    bool enabled = false;
    char name[33] {};
    int32_t latitudeE6 = 0, longitudeE6 = 0;
    uint16_t tilt = 0;
    int16_t azimuth = 0; // 0 south, -90 east, +90 west.
    uint32_t peakW = 0;
};
struct Pricing {
    bool enabled = false; // Explicit acknowledgement that all variable components are configured.
    uint16_t vatBasisPoints = 0;
    int32_t gridMicros = 0, markupMicros = 0;
    uint32_t monthlyMicros = 0; // Reporting only, excluded from marginal cost.
};
struct Settings {
    Source source = Source::Normalized;
    Pricing pricing;
    Plane planes[MaxPlanes] {};
    uint8_t planeCount = 0;
    bool enabled = false;
    char url[193] {}; // Empty: authenticated normalized JSON push; otherwise HTTP polling.
    uint32_t refreshSeconds = 900, maxAgeSeconds = 3600, horizonSeconds = 86400;
};
struct Status {
    bool enabled = false, hasData = false, fresh = false, complete = false;
    bool sourceFresh = false, transportFresh = false; // Ages, independent of tariff coverage usability.
    bool currentCovered = false, planningUsable = false, partial = false;
    uint32_t transportAgeSeconds = 0, sourceAgeSeconds = 0, nextRefresh = 0;
    uint32_t fetchedAt = 0, sourceAt = 0, horizonEnd = 0, ageSeconds = 0;
    uint16_t futureCount = 0;
    char const* state = "disabled";
    char const* error = nullptr;
};

char const* sourceName(Settings const& settings);
bool pollable(Settings const& settings);
bool effectivePrice(int32_t market, Pricing const& pricing, int32_t& result);
bool parseSettings(JsonVariantConst json, Settings& output);
void serializeSettings(Settings const& settings, JsonObject output);
bool parseSeries(JsonVariantConst json, Domain domain, Series& output, char const*& error);
char const* schema(Domain domain);

// Consumers (including a future optimizer) use only this normalized contract.
// The provider owns data; callers serialize access and never retain interval pointers across refreshes.
class Provider {
public:
    virtual ~Provider() = default;
    virtual Series const& data() const = 0;
    virtual Status status(uint32_t utc, uint32_t nowMs) const = 0;
    virtual bool refreshDue(uint32_t nowMs) const = 0;
};
enum class RetryKind : uint8_t { Local, Transport, Provider, RateLimited };
struct RefreshDecision {
    char const* reason = nullptr;
    uint32_t retryAfterSeconds = 0;
    explicit operator bool() const { return reason == nullptr; }
};

class JsonProvider final : public Provider {
public:
    explicit JsonProvider(Domain domain) : _domain(domain) {}
    void configure(Settings const& settings);
    Settings const& settings() const { return _settings; }
    Series const& data() const override { return _data; }
    Status status(uint32_t utc, uint32_t nowMs) const override;
    bool refreshDue(uint32_t nowMs) const override;
    void attempted(uint32_t nowMs);
    void failed(char const* error, uint32_t nowMs);
    void cooldown(uint32_t nowMs, uint32_t seconds, RetryKind kind);
    RefreshDecision retryDecision(uint32_t nowMs) const;
    uint32_t automaticWait(uint32_t nowMs) const;
    void defer(uint32_t nowMs, uint32_t seconds, RetryKind kind = RetryKind::Provider);
    bool retryAllowed(uint32_t nowMs) const;
    bool accept(Series const& series, uint32_t utc, uint32_t nowMs);
    Domain domain() const { return _domain; }
    char const* lastError() const { return _error; }
private:
    Domain _domain;
    Settings _settings;
    Series _data;
    uint32_t _fetchedAt = 0, _fetchedMs = 0, _attemptMs = 0;
    bool _attempted = false;
    uint32_t _retryMs = 0, _delaySeconds = 0, _providerMs = 0, _providerSeconds = 0;
    RetryKind _retryKind = RetryKind::Local, _providerKind = RetryKind::Provider;
    char const* _error = nullptr; // Stable diagnostic codes, never remote response bodies.
};

} // namespace Hems::Energy
