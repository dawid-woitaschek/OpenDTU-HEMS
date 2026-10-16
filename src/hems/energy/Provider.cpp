// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Provider.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace Hems::Energy {

char const* schema(Domain domain)
{
    return domain == Domain::Tariff ? "opendtu-hems-tariff/v1" : "opendtu-hems-forecast/v1";
}
Interval const* Series::current(uint32_t utc) const
{
    for (uint16_t i = 0; i < count; ++i) {
        if (intervals[i].start <= utc && utc < intervals[i].end) { return &intervals[i]; }
    }
    return nullptr;
}
bool Series::covers(uint32_t start, uint32_t end) const
{
    if (end <= start) { return false; }
    for (uint16_t i = 0; i < count && start < end; ++i) {
        auto const& interval = intervals[i];
        if (interval.end <= start) { continue; }
        if (interval.start > start) { return false; }
        start = interval.end;
    }
    return start >= end;
}
bool Series::energyWattSeconds(uint32_t start, uint32_t end, int64_t& result) const
{
    result = 0;
    if (!covers(start, end)) { return false; }
    for (uint16_t i = 0; i < count; ++i) {
        auto const& interval = intervals[i];
        auto from = std::max(start, interval.start), until = std::min(end, interval.end);
        if (until > from) { result += static_cast<int64_t>(interval.value) * (until - from); }
    }
    return true;
}
namespace {
bool seconds(JsonVariantConst value, uint32_t minimum, uint32_t maximum)
{
    return value.is<uint32_t>() && value.as<uint32_t>() >= minimum && value.as<uint32_t>() <= maximum;
}
bool text(JsonVariantConst value, char* output, size_t capacity)
{
    auto str = value.as<char const*>();
    if (!str || strlen(str) >= capacity) { return false; }
    for (auto p = str; *p; ++p) { if (static_cast<unsigned char>(*p) < 32) { return false; } }
    memcpy(output, str, strlen(str) + 1);
    return true;
}
} // namespace
bool parseSettings(JsonVariantConst json, Settings& output)
{
    Settings candidate;
    if (!json["enabled"].is<bool>() || !text(json["url"], candidate.url, sizeof(candidate.url))
            || !seconds(json["refresh_seconds"], 60, 86400)
            || !seconds(json["max_age_seconds"], 60, 604800)
            || !seconds(json["horizon_seconds"], 900, 604800)) { return false; }
    // Local bridge HTTP only: no implicit TLS trust bypass, credentials or redirects to cloud vendors.
    if (candidate.url[0] && (strncmp(candidate.url, "http://", 7) || strlen(candidate.url) <= 7
            || strchr(candidate.url, '@') || strchr(candidate.url, '#') || strchr(candidate.url, ' '))) { return false; }
    candidate.enabled = json["enabled"].as<bool>();
    candidate.refreshSeconds = json["refresh_seconds"];
    candidate.maxAgeSeconds = json["max_age_seconds"];
    candidate.horizonSeconds = json["horizon_seconds"];
    output = candidate;
    return true;
}
void serializeSettings(Settings const& settings, JsonObject output)
{
    output["enabled"] = settings.enabled; output["url"] = static_cast<char const*>(settings.url);
    output["refresh_seconds"] = settings.refreshSeconds; output["max_age_seconds"] = settings.maxAgeSeconds;
    output["horizon_seconds"] = settings.horizonSeconds;
}
bool parseSeries(JsonVariantConst json, Domain domain, Series& output, char const*& error)
{
    error = "invalid_payload";
    auto format = json["schema"].as<char const*>();
    auto candidate = std::unique_ptr<Series>(new (std::nothrow) Series());
    if (!candidate) { error = "insufficient_memory"; return false; }
    if (!format || strcmp(format, schema(domain)) || !text(json["source"], candidate->source, sizeof(candidate->source))
            || !candidate->source[0] || !seconds(json["generated_at"], MinUtc, UINT32_MAX)) { return false; }
    candidate->generatedAt = json["generated_at"];
    if (domain == Domain::Tariff) {
        if (!text(json["currency"], candidate->currency, sizeof(candidate->currency)) || strlen(candidate->currency) != 3) { return false; }
        for (auto c : candidate->currency) { if (c && (c < 'A' || c > 'Z')) { return false; } }
    }
    auto array = json["intervals"].as<JsonArrayConst>();
    if (array.isNull() || array.size() == 0 || array.size() > MaxIntervals) { error = "invalid_interval_count"; return false; }
    for (JsonVariantConst entry : array) {
        auto& interval = candidate->intervals[candidate->count];
        auto value = entry[domain == Domain::Tariff ? "price_micros_per_kwh" : "power_w"];
        if (!seconds(entry["start"], MinUtc, UINT32_MAX) || !seconds(entry["end"], MinUtc, UINT32_MAX)
                || !value.is<int32_t>()) { error = "invalid_interval"; return false; }
        interval.start = entry["start"]; interval.end = entry["end"]; interval.value = value;
        if (interval.end <= interval.start || interval.end - interval.start > 86400
                || (domain == Domain::Forecast && interval.value < 0)) { error = "invalid_interval"; return false; }
        if (candidate->count && interval.start < candidate->intervals[candidate->count - 1].end) {
            error = "overlapping_or_unordered_intervals"; return false;
        }
        auto importPrice = entry["import_price_micros_per_kwh"];
        if (domain == Domain::Tariff && !importPrice.isNull()) {
            if (!importPrice.is<int32_t>()) { error = "invalid_import_price"; return false; }
            interval.hasImportPrice = true; interval.importPrice = importPrice;
        }
        ++candidate->count;
    }
    output = *candidate;
    error = nullptr;
    return true;
}
void JsonProvider::configure(Settings const& settings)
{
    bool changed = strcmp(_settings.url, settings.url) || _settings.enabled != settings.enabled;
    _settings = settings;
    if (changed) { _data = {}; _fetchedAt = 0; _attempted = false; _error = nullptr; }
}
void JsonProvider::attempted(uint32_t nowMs) { _attempted = true; _attemptMs = nowMs; }
void JsonProvider::failed(char const* error, uint32_t nowMs) { attempted(nowMs); _error = error; }
bool JsonProvider::refreshDue(uint32_t nowMs) const
{
    return _settings.enabled && _settings.url[0] && (!_attempted || nowMs - _attemptMs >= _settings.refreshSeconds * 1000);
}
bool JsonProvider::accept(Series const& series, uint32_t utc, uint32_t nowMs)
{
    if (!_settings.enabled) { failed("provider_disabled", nowMs); return false; }
    if (utc < MinUtc || series.generatedAt > static_cast<uint64_t>(utc) + 300) { failed("clock_or_source_time_invalid", nowMs); return false; }
    if (series.count == 0 || series.count > MaxIntervals) { failed("invalid_interval_count", nowMs); return false; }
    if (_data.count && series.generatedAt < _data.generatedAt) { failed("older_source_timestamp", nowMs); return false; }
    _data = series; _fetchedAt = utc; _fetchedMs = nowMs; attempted(nowMs); _error = nullptr;
    return true;
}
Status JsonProvider::status(uint32_t utc, uint32_t nowMs) const
{
    Status result;
    result.enabled = _settings.enabled; result.hasData = _data.count != 0;
    result.error = _error; result.fetchedAt = _fetchedAt; result.sourceAt = _data.generatedAt;
    if (_data.count) {
        result.horizonEnd = _data.intervals[_data.count - 1].end;
        for (uint16_t i = 0; i < _data.count; ++i) { if (_data.intervals[i].end > utc) { ++result.futureCount; } }
        result.ageSeconds = (nowMs - _fetchedMs) / 1000;
        if (utc >= _data.generatedAt) { result.ageSeconds = std::max(result.ageSeconds, utc - _data.generatedAt); }
    }
    bool clock = utc >= MinUtc && utc >= _fetchedAt && _data.generatedAt <= static_cast<uint64_t>(utc) + 300;
    result.fresh = result.enabled && result.hasData && clock && result.ageSeconds <= _settings.maxAgeSeconds;
    uint64_t end = static_cast<uint64_t>(utc) + _settings.horizonSeconds;
    result.complete = result.hasData && end <= UINT32_MAX && _data.covers(utc, static_cast<uint32_t>(end));
    result.state = !result.enabled ? "disabled" : utc < MinUtc ? "waiting_for_time"
        : !result.hasData ? (_error ? "error" : "unavailable") : !result.fresh ? "stale"
        : _error ? "error" : !result.complete ? "partial" : "ready";
    return result;
}

} // namespace Hems::Energy
