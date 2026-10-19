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
    char const* zone = s + 19;
    if (*zone == '.') {
        ++zone;
        if (*zone < '0' || *zone > '9') { return false; }
        while (*zone >= '0' && *zone <= '9') { ++zone; }
    }
    int offset = 0;
    if (*zone == 'Z' && !zone[1]) { /* explicit UTC */ }
    else if ((*zone == '+' || *zone == '-') && strlen(zone) == 6 && zone[3] == ':') {
        int oh, om, end = 0;
        if (sscanf(zone + 1, "%2d:%2d%n", &oh, &om, &end) != 2 || end != 5 || oh > 14 || om > 59 || oh < 0 || om < 0) { return false; }
        offset = (oh * 60 + om) * 60 * (*zone == '+' ? 1 : -1);
    } else if (!bareUtc || *zone) { return false; }
    y -= m <= 2;
    int era = y / 400, year = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = era * 146097 + year * 365 + year / 4 - year / 100 + doy - 719468;
    int64_t result = days * 86400 + h * 3600 + min * 60 + sec - offset;
    if (result < MinUtc || result > UINT32_MAX) { return false; }
    utc = result; return true;
}
uint32_t retryAfterSeconds(char const* text, uint32_t now)
{
    if (!text || !*text) { return 0; }
    uint64_t seconds = 0; bool digits = true;
    for (auto p = text; *p; ++p) {
        if (*p < '0' || *p > '9') { digits = false; break; }
        seconds = std::min(static_cast<uint64_t>(UINT32_MAX), seconds * 10 + unsigned(*p - '0'));
    }
    if (digits) { return seconds; }
    char weekday[4] {}, month[4] {}; unsigned day, year, hour, minute, second; int end = 0;
    if (sscanf(text, "%3s, %2u %3s %4u %2u:%2u:%2u GMT%n", weekday, &day, month, &year, &hour, &minute, &second, &end) != 7
            || !end || text[end]) { return 0; }
    constexpr char Months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    unsigned m = 0; for (unsigned i = 0; i < 12; ++i) { if (!strncmp(month, Months + 3 * i, 3)) { m = i + 1; break; } }
    if (!m) { return 0; }
    if (year > 2105) { return UINT32_MAX; } // Beyond our UTC contract: hold conservatively.
    char iso[32]; snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02uZ", year, m, day, hour, minute, second);
    uint32_t date;
    if (!parseUtc(iso, date) || date <= now) { return 0; }
    return date - now;
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
    snprintf(out, capacity, "https://api.energy-charts.info/v2/price?bzn=DE-LU&start=%" PRIu32 "&end=%" PRIu32 "",
        start, end - 1);
}
bool parseEnergyCharts(JsonVariantConst j, uint32_t now, Series& out, char const*& error)
{
    error = "invalid_energy_charts_payload";
    char const* schema = j["schema_version"] | "";
    if (strncmp(schema, "2.", 2) || strcmp(j["endpoint"] | "", "price")
            || strcmp(j["bidding_zone"] | "", "DE-LU") || strcmp(j["timezone"] | "", "Europe/Berlin")) { return false; }
    if (!j["deprecated"].is<bool>()) { return false; }
    if (j["deprecated"].as<bool>()) { error = "energy_charts_endpoint_deprecated"; return false; }
    if (!j["interval_minutes"].is<unsigned>()) { return false; }
    unsigned intervalMinutes = j["interval_minutes"].as<unsigned>();
    if (intervalMinutes != 15 && intervalMinutes != 60) { return false; }
    uint32_t resolution = intervalMinutes * 60;
    uint32_t generatedAt;
    if (!parseUtc(j["generated_at"] | static_cast<char const*>(nullptr), generatedAt)
            || (generatedAt > now && generatedAt - now > 300)) { return false; }

    auto series = j["series"].as<JsonArrayConst>(), data = j["data"].as<JsonArrayConst>();
    if (series.size() != 1 || data.size() < 2) { return false; }
    if (data.size() > MaxIntervals) { error = "invalid_interval_count"; return false; }
    char const* seriesId = series[0]["id"] | "";
    if (!*seriesId) { return false; }
    char const* seriesUnit = series[0]["unit"] | "";
    char const* unit = *seriesUnit ? seriesUnit : (j["unit"] | "");
    if (strcmp(unit, "EUR/MWh") && strcmp(unit, "EUR / MWh")) { return false; }

    // Validate the complete v2 envelope before touching the destination. Missing
    // price rows stay real gaps; they are never stretched across unpublished data.
    uint32_t previous = 0;
    size_t published = 0;
    for (JsonVariantConst row : data) {
        uint32_t timestamp;
        if (!parseUtc(row["timestamp"] | static_cast<char const*>(nullptr), timestamp)
                || timestamp > UINT32_MAX - resolution) { return false; }
        if (previous && (timestamp <= previous || (timestamp - previous) % resolution)) { return false; }
        previous = timestamp;
        auto values = row["values"].as<JsonObjectConst>();
        if (values.size() != 1 || !values.containsKey(seriesId)) { return false; }
        auto value = values[seriesId];
        if (value.isNull()) { continue; }
        if (!value.is<double>() || value.is<bool>()) { return false; }
        double micros = value.as<double>() * 1000.0;
        if (!std::isfinite(micros) || micros < INT32_MIN || micros > INT32_MAX) { return false; }
        ++published;
    }
    if (!published) { error = "prices_not_published"; return false; }

    out.count = 0; out.generatedAt = generatedAt; out.sourceTimestampKnown = true;
    snprintf(out.source, sizeof(out.source), "%s", "Energy-Charts.info / SMARD");
    snprintf(out.currency, sizeof(out.currency), "%s", "EUR");
    for (JsonVariantConst row : data) {
        auto value = row["values"][seriesId];
        if (value.isNull()) { continue; }
        auto& interval = out.intervals[out.count++]; interval = {};
        parseUtc(row["timestamp"] | static_cast<char const*>(nullptr), interval.start);
        interval.end = interval.start + resolution;
        interval.value = std::llround(value.as<double>() * 1000.0);
    }
    error = nullptr; return true;
}

bool parseForecastSolar(JsonVariantConst j, Series& out, NativeInfo& info, char const*& error)
{
    error = "invalid_forecast_solar_payload";
    auto message = j["message"];
    if (!message["code"].is<int>() || message["code"].as<int>() != 0 || strcmp(message["type"] | "", "success")) {
        if (message["code"] == 429) { info.retrySeconds = std::max(info.retrySeconds, static_cast<uint32_t>(7200)); error = "provider_rate_limited"; }
        else { error = "forecast_solar_error"; }
        return false;
    }
    if (message["ratelimit"]["limit"].is<int32_t>()) { info.rateLimit = message["ratelimit"]["limit"]; }
    if (message["ratelimit"]["remaining"].is<int32_t>()) { info.rateRemaining = message["ratelimit"]["remaining"]; }
    if (info.rateRemaining == 0) { info.retrySeconds = std::max(info.retrySeconds, std::max(static_cast<uint32_t>(7200), message["ratelimit"]["period"] | static_cast<uint32_t>(0))); }
    uint32_t generatedAt;
    if (!parseUtc(message["info"]["time_utc"], generatedAt)) { return false; }
    auto periods = j["result"]["watt_hours_period"].as<JsonObjectConst>();
    if (periods.size() < 2 || periods.size() > MaxIntervals + 1) { error = "invalid_interval_count"; return false; }
    // Two bounded passes preserve the parser's failure atomicity without allocating
    // a second Series. Re-reading <=257 UTC keys is cheap compared with HTTPS.
    uint32_t previous = 0;
    for (auto entry : periods) {
        uint32_t end;
        if (!parseUtc(entry.key().c_str(), end, true) || !entry.value().is<uint32_t>()) { return false; }
        uint32_t wh = entry.value();
        if (!previous) { if (wh != 0) { return false; } previous = end; continue; }
        if (end <= previous || end - previous > 3600) { error = "forecast_interval_gap"; return false; }
        uint64_t watts = (static_cast<uint64_t>(wh) * 3600 + (end - previous) / 2) / (end - previous);
        if (watts > INT32_MAX) { return false; }
        previous = end;
    }
    out.count = 0; out.generatedAt = generatedAt; out.sourceTimestampKnown = true; out.currency[0] = 0;
    snprintf(out.source, sizeof(out.source), "%s", "Forecast.Solar");
    previous = 0;
    for (auto entry : periods) {
        uint32_t end = 0; parseUtc(entry.key().c_str(), end, true);
        if (previous) {
            auto& interval = out.intervals[out.count++]; interval = {};
            interval.start = previous; interval.end = end;
            interval.value = (static_cast<uint64_t>(entry.value().as<uint32_t>()) * 3600 + (end - previous) / 2) / (end - previous);
        }
        previous = end;
    }
    error = nullptr; return true;
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
