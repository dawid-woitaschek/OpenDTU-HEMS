// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cinttypes>
#include <memory>
#include <new>

namespace Hems::Energy {
// Gregorian civil date -> UTC, independent of process TZ and DST.
bool parseUtc(char const* s, uint32_t& utc, bool bareUtc)
{
    if (!s) { return false; }
    int y, m, d, h, min, sec, used = 0;
    if (sscanf(s, "%4d-%2d-%2d%*c%2d:%2d:%2d%n", &y, &m, &d, &h, &min, &sec, &used) != 6
            || used != 19 || (s[10] != 'T' && s[10] != ' ') || y < 2020 || y > 2105
            || m < 1 || m > 12 || d < 1 || h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) { return false; }
    int daysInMonth[] = { 31, 28 + ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (d > daysInMonth[m - 1]) { return false; }
    int offset = 0;
    if (s[19] == 'Z' && !s[20]) { /* explicit UTC */ }
    else if ((s[19] == '+' || s[19] == '-') && strlen(s) == 25 && s[22] == ':') {
        int oh, om, end = 0;
        if (sscanf(s + 20, "%2d:%2d%n", &oh, &om, &end) != 2 || end != 5 || oh > 14 || om > 59 || oh < 0 || om < 0) { return false; }
        offset = (oh * 60 + om) * 60 * (s[19] == '+' ? 1 : -1);
    } else if (!bareUtc || s[19]) { return false; }
    y -= m <= 2;
    int era = y / 400, year = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = era * 146097 + year * 365 + year / 4 - year / 100 + doy - 719468;
    int64_t result = days * 86400 + h * 3600 + min * 60 + sec - offset;
    if (result < MinUtc || result > UINT32_MAX) { return false; }
    utc = result; return true;
}
void forecastUrl(Plane const& p, char* out, size_t capacity)
{
    snprintf(out, capacity, "https://api.forecast.solar/estimate/%.6f/%.6f/%u/%d/%.3f?time=utc&full=1",
        p.latitudeE6 / 1000000.0, p.longitudeE6 / 1000000.0, unsigned(p.tilt), static_cast<int>(p.azimuth), p.peakW / 1000.0);
}
namespace {
uint32_t germanOffset(uint32_t utc)
{
    time_t value = utc; struct tm date {}; gmtime_r(&value, &date);
    auto transition = [&](unsigned month) {
        char text[32]; snprintf(text, sizeof(text), "%04d-%02u-31T01:00:00Z", date.tm_year + 1900, month);
        uint32_t last = 0; parseUtc(text, last); time_t day = last; struct tm end {}; gmtime_r(&day, &end);
        return last - static_cast<uint32_t>(end.tm_wday) * 86400;
    };
    return utc >= transition(3) && utc < transition(10) ? 7200 : 3600;
}
} // namespace
void germanyDay(uint32_t utc, unsigned ahead, uint32_t& start, uint32_t& end)
{
    uint32_t local = utc + germanOffset(utc);
    uint32_t day = local - local % 86400 + ahead * 86400;
    start = day - germanOffset(day); end = day + 86400 - germanOffset(day + 86400);
}
void tariffUrl(uint32_t utc, char* out, size_t capacity)
{
    uint32_t start, end, unused; germanyDay(utc, 0, start, unused); germanyDay(utc, 1, unused, end);
    snprintf(out, capacity, "https://api.energy-charts.info/price?bzn=DE-LU&start=%" PRIu32 "&end=%" PRIu32 "",
        start, end - 1);
}
bool parseEnergyCharts(JsonVariantConst j, uint32_t now, Series& out, char const*& error)
{
    error = "invalid_energy_charts_payload";
    auto times = j["unix_seconds"].as<JsonArrayConst>(), values = j["price"].as<JsonArrayConst>();
    if (strcmp(j["unit"] | "", "EUR / MWh") || times.size() < 2 || times.size() != values.size()) { return false; }
    if (times.size() > MaxIntervals) { error = "invalid_interval_count"; return false; }
    uint32_t resolution = 3600;
    for (size_t i = 0; i < times.size(); ++i) {
        if (!times[i].is<uint32_t>() || times[i].as<uint32_t>() < MinUtc) { return false; }
        if (i) {
            auto prev = times[i - 1].as<uint32_t>(), t = times[i].as<uint32_t>();
            if (t <= prev || (t - prev) % 900) { return false; }
            resolution = std::min(resolution, t - prev);
        }
    }
    if (resolution != 900 && resolution != 3600) { return false; }
    auto candidate = std::unique_ptr<Series>(new (std::nothrow) Series());
    if (!candidate) { error = "insufficient_memory"; return false; }
    candidate->generatedAt = now; candidate->sourceTimestampKnown = false;
    snprintf(candidate->source, sizeof(candidate->source), "%s", "Energy-Charts.info / SMARD"); snprintf(candidate->currency, sizeof(candidate->currency), "%s", "EUR");
    for (size_t i = 0; i < times.size(); ++i) {
        uint32_t t = times[i];
        if (t > UINT32_MAX - resolution) { return false; }
        if (values[i].isNull()) { continue; } // Not published: leave a gap.
        if (!values[i].is<double>() || values[i].is<bool>()) { return false; }
        double micros = values[i].as<double>() * 1000.0; // EUR/MWh -> micro EUR/kWh.
        if (!std::isfinite(micros) || micros < INT32_MIN || micros > INT32_MAX) { return false; }
        auto& interval = candidate->intervals[candidate->count++];
        interval.start = t; interval.end = t + resolution; interval.value = std::llround(micros);
    }
    if (!candidate->count) { error = "prices_not_published"; return false; }
    out = *candidate; error = nullptr; return true;
}
bool parseForecastSolar(JsonVariantConst j, Series& out, NativeInfo& info, char const*& error)
{
    error = "invalid_forecast_solar_payload";
    auto message = j["message"];
    if (!message["code"].is<int>() || message["code"].as<int>() != 0 || strcmp(message["type"] | "", "success")) {
        if (message["code"] == 429) { info.retrySeconds = 7200; error = "provider_rate_limited"; }
        else { error = "forecast_solar_error"; }
        return false;
    }
    if (message["ratelimit"]["limit"].is<int32_t>()) { info.rateLimit = message["ratelimit"]["limit"]; }
    if (message["ratelimit"]["remaining"].is<int32_t>()) { info.rateRemaining = message["ratelimit"]["remaining"]; }
    if (info.rateRemaining == 0) { info.retrySeconds = std::max(static_cast<uint32_t>(7200), message["ratelimit"]["period"] | static_cast<uint32_t>(0)); }
    auto candidate = std::unique_ptr<Series>(new (std::nothrow) Series());
    if (!candidate) { error = "insufficient_memory"; return false; }
    if (!parseUtc(message["info"]["time_utc"], candidate->generatedAt)) { return false; }
    snprintf(candidate->source, sizeof(candidate->source), "%s", "Forecast.Solar");
    auto periods = j["result"]["watt_hours_period"].as<JsonObjectConst>();
    if (periods.size() < 2 || periods.size() > MaxIntervals + 1) { error = "invalid_interval_count"; return false; }
    uint32_t previous = 0;
    for (auto entry : periods) {
        uint32_t end;
        if (!parseUtc(entry.key().c_str(), end, true) || !entry.value().is<uint32_t>()) { return false; }
        uint32_t wh = entry.value();
        if (!previous) { if (wh != 0) { return false; } previous = end; continue; }
        // full=1 supplies the complete UTC series, including zero production at night.
        if (end <= previous || end - previous > 3600) { error = "forecast_interval_gap"; return false; }
        uint64_t watts = (static_cast<uint64_t>(wh) * 3600 + (end - previous) / 2) / (end - previous);
        if (watts > INT32_MAX) { return false; }
        auto& interval = candidate->intervals[candidate->count++];
        interval.start = previous; interval.end = end; interval.value = watts; previous = end;
    }
    out = *candidate; error = nullptr; return true;
}
bool aggregate(Series const& a, Series const& b, Series& out, char const*& error)
{
    error = "plane_horizon_mismatch";
    if (!a.count || !b.count) { return false; }
    auto result = std::unique_ptr<Series>(new (std::nothrow) Series());
    if (!result) { error = "insufficient_memory"; return false; }
    uint32_t start = std::max(a.intervals[0].start, b.intervals[0].start);
    uint32_t finish = std::min(a.intervals[a.count - 1].end, b.intervals[b.count - 1].end);
    while (start < finish) {
        auto x = a.current(start), y = b.current(start);
        if (!x || !y) { return false; }
        uint32_t end = std::min(x->end, y->end);
        int64_t sum = static_cast<int64_t>(x->value) + y->value;
        if (result->count == MaxIntervals || sum > INT32_MAX) { error = "aggregate_capacity_exceeded"; return false; }
        auto& i = result->intervals[result->count++]; i.start = start; i.end = end; i.value = sum;
        start = end;
    }
    if (!result->count) { return false; }
    result->generatedAt = std::min(a.generatedAt, b.generatedAt); snprintf(result->source, sizeof(result->source), "%s", "Forecast.Solar");
    out = *result; error = nullptr; return true;
}
} // namespace Hems::Energy
