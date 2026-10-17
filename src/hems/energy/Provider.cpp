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
char const* sourceName(Settings const& s)
{
    return s.source == Source::EnergyCharts ? "energy_charts" : s.source == Source::ForecastSolar ? "forecast_solar"
        : s.url[0] ? "normalized_http" : "normalized_push";
}
bool pollable(Settings const& s) { return s.source != Source::Normalized || s.url[0]; }
bool effectivePrice(int32_t market, Pricing const& p, int32_t& result)
{
    if (!p.enabled) { return false; }
    int64_t net = static_cast<int64_t>(market) + p.gridMicros + p.markupMicros;
    int64_t gross = net * (10000 + p.vatBasisPoints);
    gross = (gross + (gross >= 0 ? 5000 : -5000)) / 10000;
    if (gross < INT32_MIN || gross > INT32_MAX) { return false; }
    result = gross; return true;
}
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
    auto source = json["provider"].as<char const*>();
    if (source && strcmp(source, "normalized_push") && strcmp(source, "normalized_http")) {
        if (!strcmp(source, "energy_charts")) { candidate.source = Source::EnergyCharts; }
        else if (!strcmp(source, "forecast_solar")) { candidate.source = Source::ForecastSolar; }
        else { return false; }
    }
    if (source && !strcmp(source, "normalized_http") && !candidate.url[0]) { return false; }
    if (candidate.source != Source::Normalized && candidate.url[0]) { return false; }
    auto pricing = json["pricing"];
    if (!pricing.isNull()) {
        if (!pricing["enabled"].is<bool>() || !seconds(pricing["vat_basis_points"], 0, 10000)
                || !pricing["grid_micros_per_kwh"].is<int32_t>() || !pricing["markup_micros_per_kwh"].is<int32_t>()
                || !pricing["monthly_micros"].is<uint32_t>()) { return false; }
        candidate.pricing = { pricing["enabled"], pricing["vat_basis_points"], pricing["grid_micros_per_kwh"],
            pricing["markup_micros_per_kwh"], pricing["monthly_micros"] };
    }
    auto planes = json["planes"].as<JsonArrayConst>();
    if (!json["planes"].isNull() && planes.isNull()) { return false; }
    if (planes.size() > MaxPlanes) { return false; }
    unsigned enabled = 0;
    for (JsonVariantConst entry : planes) {
        auto& p = candidate.planes[candidate.planeCount++];
        if (!entry["enabled"].is<bool>() || !text(entry["name"], p.name, sizeof(p.name)) || !p.name[0]
                || !entry["latitude_e6"].is<int32_t>() || !entry["longitude_e6"].is<int32_t>()
                || !seconds(entry["tilt"], 0, 90) || !entry["azimuth"].is<int16_t>()
                || !seconds(entry["peak_w"], 1, 1000000)) { return false; }
        p.enabled = entry["enabled"]; p.latitudeE6 = entry["latitude_e6"]; p.longitudeE6 = entry["longitude_e6"];
        p.tilt = entry["tilt"]; p.azimuth = entry["azimuth"]; p.peakW = entry["peak_w"];
        if (p.latitudeE6 < -90000000 || p.latitudeE6 > 90000000 || p.longitudeE6 < -180000000 || p.longitudeE6 > 180000000
                || p.azimuth < -180 || p.azimuth > 180) { return false; }
        if (p.enabled) { ++enabled; }
    }
    if (candidate.source == Source::ForecastSolar && json["enabled"].as<bool>() && !enabled) { return false; }
    candidate.enabled = json["enabled"].as<bool>();
    candidate.refreshSeconds = json["refresh_seconds"];
    candidate.maxAgeSeconds = json["max_age_seconds"];
    candidate.horizonSeconds = json["horizon_seconds"];
    output = candidate;
    return true;
}
void serializeSettings(Settings const& settings, JsonObject output)
{
    output["provider"] = sourceName(settings);
    auto price = output["pricing"].to<JsonObject>();
    price["enabled"] = settings.pricing.enabled; price["vat_basis_points"] = settings.pricing.vatBasisPoints;
    price["grid_micros_per_kwh"] = settings.pricing.gridMicros; price["markup_micros_per_kwh"] = settings.pricing.markupMicros;
    price["monthly_micros"] = settings.pricing.monthlyMicros;
    auto planes = output["planes"].to<JsonArray>();
    for (size_t i = 0; i < settings.planeCount; ++i) {
        auto const& p = settings.planes[i]; auto plane = planes.add<JsonObject>();
        plane["enabled"] = p.enabled; plane["name"] = static_cast<char const*>(p.name);
        plane["latitude_e6"] = p.latitudeE6; plane["longitude_e6"] = p.longitudeE6;
        plane["tilt"] = p.tilt; plane["azimuth"] = p.azimuth; plane["peak_w"] = p.peakW;
    }
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
    bool changed = strcmp(_settings.url, settings.url) || _settings.enabled != settings.enabled || _settings.source != settings.source
        || _settings.planeCount != settings.planeCount;
    for (size_t i = 0; i < settings.planeCount && !changed; ++i) {
        auto const& a = _settings.planes[i]; auto const& b = settings.planes[i];
        changed = a.enabled != b.enabled || a.latitudeE6 != b.latitudeE6 || a.longitudeE6 != b.longitudeE6
            || a.tilt != b.tilt || a.azimuth != b.azimuth || a.peakW != b.peakW;
    }
    bool priceChanged = _settings.pricing.enabled != settings.pricing.enabled || _settings.pricing.vatBasisPoints != settings.pricing.vatBasisPoints
        || _settings.pricing.gridMicros != settings.pricing.gridMicros || _settings.pricing.markupMicros != settings.pricing.markupMicros;
    changed = changed || priceChanged;
    _settings = settings;
    if (changed) { _data.count = 0; _data.generatedAt = 0; _data.source[0] = 0; _data.currency[0] = 0; _fetchedAt = 0; _attempted = false; _delaySeconds = 0; _error = nullptr; }
}
void JsonProvider::attempted(uint32_t nowMs) { _attempted = true; _attemptMs = nowMs; }
void JsonProvider::failed(char const* error, uint32_t nowMs) { attempted(nowMs); _error = error; }
bool JsonProvider::refreshDue(uint32_t nowMs) const
{
    uint32_t minimum = _settings.source == Source::ForecastSolar ? 7200 : _settings.source == Source::EnergyCharts ? 3600 : 60;
    return _settings.enabled && pollable(_settings) && retryAllowed(nowMs)
        && (!_attempted || nowMs - _attemptMs >= std::max(minimum, _settings.refreshSeconds) * 1000);
}
void JsonProvider::defer(uint32_t nowMs, uint32_t seconds) { _retryMs = nowMs; _delaySeconds = std::min(seconds, static_cast<uint32_t>(604800)); }
bool JsonProvider::retryAllowed(uint32_t nowMs) const { return !_delaySeconds || nowMs - _retryMs >= _delaySeconds * 1000; }
bool JsonProvider::accept(Series const& series, uint32_t utc, uint32_t nowMs)
{
    if (!_settings.enabled) { failed("provider_disabled", nowMs); return false; }
    if (utc < MinUtc || (_fetchedAt && utc < _fetchedAt) || series.generatedAt > static_cast<uint64_t>(utc) + 300) { failed("clock_or_source_time_invalid", nowMs); return false; }
    if (series.count == 0 || series.count > MaxIntervals) { failed("invalid_interval_count", nowMs); return false; }
    if (_data.count && series.generatedAt < _data.generatedAt) { failed("older_source_timestamp", nowMs); return false; }
    if (_domain == Domain::Tariff && _settings.pricing.enabled) {
        for (size_t i = 0; i < series.count; ++i) {
            int32_t value;
            if (!effectivePrice(series.intervals[i].value, _settings.pricing, value)) {
                failed("effective_price_overflow", nowMs); return false;
            }
        }
    }
    _data = series;
    if (_domain == Domain::Tariff && _settings.pricing.enabled) {
        for (size_t i = 0; i < _data.count; ++i) {
            effectivePrice(_data.intervals[i].value, _settings.pricing, _data.intervals[i].importPrice);
            _data.intervals[i].hasImportPrice = true;
        }
    }
    _fetchedAt = utc; _fetchedMs = nowMs; attempted(nowMs); _error = nullptr;
    return true;
}
Status JsonProvider::status(uint32_t utc, uint32_t nowMs) const
{
    Status result;
    result.enabled = _settings.enabled; result.hasData = _data.count != 0;
    result.error = _error; result.fetchedAt = _fetchedAt; result.sourceAt = _data.sourceTimestampKnown ? _data.generatedAt : 0;
    if (_data.count) {
        result.horizonEnd = _data.intervals[_data.count - 1].end;
        for (uint16_t i = 0; i < _data.count; ++i) { if (_data.intervals[i].end > utc) { ++result.futureCount; } }
        result.transportAgeSeconds = (nowMs - _fetchedMs) / 1000;
        result.ageSeconds = result.transportAgeSeconds;
        if (utc >= _data.generatedAt) { result.ageSeconds = std::max(result.ageSeconds, utc - _data.generatedAt); }
    }
    result.sourceAgeSeconds = utc >= _data.generatedAt ? utc - _data.generatedAt : 0;
    uint32_t minimum = _settings.source == Source::ForecastSolar ? 7200 : _settings.source == Source::EnergyCharts ? 3600 : 60;
    uint32_t until = _attempted ? std::max(minimum, _settings.refreshSeconds) - std::min(std::max(minimum, _settings.refreshSeconds), (nowMs - _attemptMs) / 1000) : 0;
    uint32_t retry = _delaySeconds - std::min(_delaySeconds, (nowMs - _retryMs) / 1000);
    if (_settings.enabled && pollable(_settings)) { result.nextRefresh = utc + std::max(until, retry); }
    bool clock = utc >= MinUtc && utc >= _fetchedAt && _data.generatedAt <= static_cast<uint64_t>(utc) + 300;
    result.fresh = result.enabled && result.hasData && clock && (_domain == Domain::Tariff || result.ageSeconds <= _settings.maxAgeSeconds);
    uint64_t end = static_cast<uint64_t>(utc) + _settings.horizonSeconds;
    result.complete = result.hasData && end <= UINT32_MAX && _data.covers(utc, static_cast<uint32_t>(end));
    result.currentCovered = result.hasData && _data.current(utc);
    result.partial = result.currentCovered && !result.complete;
    result.planningUsable = result.fresh && result.currentCovered;
    result.state = !result.enabled ? "disabled" : utc < MinUtc ? "waiting_for_time"
        : !result.hasData ? (_error ? "error" : "unavailable") : !result.fresh ? "stale"
        : !result.currentCovered ? "missing_current_interval" : !result.complete ? "partial" : "ready";
    return result;
}

} // namespace Hems::Energy
