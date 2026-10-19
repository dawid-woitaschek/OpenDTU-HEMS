// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
using namespace Hems::Energy;
constexpr uint32_t Now = 1792180800;
void isoUtc(uint32_t timestamp, char* out, size_t capacity)
{
    time_t value = timestamp; struct tm utc {}; gmtime_r(&value, &utc);
    strftime(out, capacity, "%Y-%m-%dT%H:%M:%SZ", &utc);
}
JsonDocument prices(unsigned count, uint32_t first = Now, unsigned resolution = 900)
{
    JsonDocument d;
    d["schema_version"] = "2.0"; d["endpoint"] = "price"; d["bidding_zone"] = "DE-LU";
    d["timezone"] = "Europe/Berlin"; d["interval_minutes"] = resolution / 60; d["unit"] = "EUR/MWh";
    d["generated_at"] = "2026-10-16T19:59:00.250+00:00"; d["deprecated"] = false;
    auto series = d["series"].add<JsonObject>(); series["id"] = "day_ahead_price"; series["name"] = "Day-ahead spot market price";
    auto data = d["data"].to<JsonArray>();
    for (unsigned i = 0; i < count; ++i) {
        char timestamp[32]; isoUtc(first + i * resolution, timestamp, sizeof(timestamp));
        auto row = data.add<JsonObject>(); row["timestamp"] = timestamp;
        row["values"]["day_ahead_price"] = i ? 123.456 : -55.5;
    }
    return d;
}
void setPriceTimestamp(JsonDocument& d, size_t index, uint32_t timestamp)
{
    char text[32]; isoUtc(timestamp, text, sizeof(text)); d["data"][index]["timestamp"] = text;
}
JsonDocument solar()
{
    JsonDocument d; d["message"]["code"] = 0; d["message"]["type"] = "success";
    d["message"]["info"]["time_utc"] = "2026-10-17T00:00:00+00:00";
    d["message"]["ratelimit"]["limit"] = 12; d["message"]["ratelimit"]["remaining"] = 5;
    auto p = d["result"]["watt_hours_period"].to<JsonObject>();
    p["2026-10-17 00:00:00"] = 0; p["2026-10-17 01:00:00"] = 0;
    p["2026-10-17 02:00:00"] = 1000; p["2026-10-17 03:00:00"] = 500;
    return d;
}
int main()
{
    Series a, b, output; char const* error; NativeInfo info;
    for (unsigned count : {92, 100, 192, 196, 256}) {
        auto doc = prices(count);
        assert(parseEnergyCharts(doc.as<JsonVariantConst>(), Now, output, error)); assert(output.count == count);
        assert(output.intervals[0].value == -55500 && output.intervals[1].value == 123456);
        assert(output.covers(Now, Now + count * 900)); assert(output.sourceTimestampKnown && output.generatedAt == Now - 60);
    }
    auto d = prices(257); assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error)); assert(!strcmp(error, "invalid_interval_count"));
    d = prices(96); d["data"][10]["values"]["day_ahead_price"] = nullptr;
    assert(parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error)); assert(!output.covers(Now, Now + 86400));
    d = prices(4); setPriceTimestamp(d, 3, Now + 4 * 900);
    assert(parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error)); assert(!output.current(Now + 3 * 900));
    d = prices(4, Now, 3600); assert(parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error)); assert(output.intervals[3].end == Now + 14400);
    d["unit"] = "EUR/kWh"; assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    auto saved = output;
    d = prices(4); d["data"][3]["values"]["day_ahead_price"] = "1.2"; assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    assert(!memcmp(&saved, &output, sizeof(output)));
    d = prices(4); d["data"][1]["values"]["day_ahead_price"] = 1e30; assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    d = prices(4); setPriceTimestamp(d, 1, Now); assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    d = prices(4); d["deprecated"] = true; assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    assert(!strcmp(error, "energy_charts_endpoint_deprecated"));
    d = prices(4); d["endpoint"] = "price_current"; assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    d = prices(4); d["series"][0]["unit"] = "EUR/kWh"; assert(!parseEnergyCharts(d.as<JsonVariantConst>(), Now, output, error));
    char tariff[192]; tariffUrl(Now, tariff, sizeof(tariff));
    assert(!strncmp(tariff, "https://api.energy-charts.info/v2/price?", strlen("https://api.energy-charts.info/v2/price?")));
    uint32_t dateStart; assert(parseUtc("2026-10-17T00:00:00Z", dateStart));
    assert(parseUtc("2026-10-17T00:00:00.123456Z", dateStart));
    assert(retryAfterSeconds("Sat, 17 Oct 2026 01:00:00 GMT", dateStart) == 3600);
    assert(retryAfterSeconds("Sat, 17 Oct 2026 00:00:00 GMT", dateStart) == 0);
    assert(retryAfterSeconds("86400", dateStart) == 86400 && retryAfterSeconds("999999999999999", dateStart) == UINT32_MAX);
    assert(retryAfterSeconds("not-a-date", dateStart) == 0 && retryAfterSeconds("-1", dateStart) == 0);
    uint32_t x, y, begin, end;
    assert(parseUtc("2026-03-29T12:00:00Z", x)); germanyDay(x, 0, begin, end); assert(end-begin==23*3600);
    assert(parseUtc("2026-10-25T12:00:00Z", x)); germanyDay(x, 0, begin, end); assert(end-begin==25*3600);
    assert(parseUtc("2026-10-24T23:00:00Z", x)); germanyDay(x, 0, begin, end); assert(end-begin==25*3600);
    assert(parseUtc("2026-10-25T02:30:00+02:00", x)); assert(parseUtc("2026-10-25T02:30:00+01:00", y)); assert(y - x == 3600);
    assert(parseUtc("2026-10-25T02:30:00.500+02:00", x)); assert(parseUtc("2026-10-25T02:30:00.750+01:00", y)); assert(y - x == 3600);
    assert(parseUtc("2026-03-29T01:30:00+01:00", x)); assert(parseUtc("2026-03-29T03:30:00+02:00", y)); assert(y - x == 3600);
    for (auto bad : {"2026-02-30T00:00:00Z", "2026-10-17 12:00:00", "2026-10-17T25:00:00Z", "2026-10-17T00:00:00+20:00", "2026-10-17T00:00:00.Z"}) { assert(!parseUtc(bad, x)); }
    d = solar(); assert(parseForecastSolar(d.as<JsonVariantConst>(), a, info, error)); assert(a.count == 3 && a.intervals[0].value == 0);
    assert(a.intervals[1].value == 1000 && info.rateRemaining == 5);
    b = a; for (size_t i = 0; i < b.count; ++i) { b.intervals[i].value *= 2; }
    assert(aggregate(a, b, output, error)); assert(output.intervals[1].value == 3000);
    b.intervals[0].end -= 100; assert(!aggregate(a, b, output, error));
    saved = a;
    d["result"]["watt_hours_period"].remove("2026-10-17 01:00:00"); assert(!parseForecastSolar(d.as<JsonVariantConst>(), a, info, error));
    assert(!memcmp(&saved, &a, sizeof(a)));
    d = solar(); d["message"]["code"] = 429; assert(!parseForecastSolar(d.as<JsonVariantConst>(), a, info, error)); assert(info.retrySeconds >= 7200);
    d = solar(); d["message"]["code"] = 500; assert(!parseForecastSolar(d.as<JsonVariantConst>(), a, info, error));
    d = solar(); d["message"]["ratelimit"]["remaining"] = 0; info = {};
    info.retrySeconds = 86400;
    assert(parseForecastSolar(d.as<JsonVariantConst>(), a, info, error) && info.retrySeconds == 86400);
    // Full bounded multi-plane horizon; east/west power patterns use independent intervals.
    a = {}; a.generatedAt = Now; a.count = 256;
    for (size_t i = 0; i < a.count; ++i) { a.intervals[i] = {Now + uint32_t(i)*900, Now + uint32_t(i+1)*900, int32_t(i%2 ? 1000 : 0), 0, false}; }
    b = a; assert(aggregate(a,b,output,error)); assert(output.count == 256 && output.intervals[255].value == 2000);
    Pricing p; p.enabled = true; p.vatBasisPoints = 1900; p.gridMicros = 100000; p.markupMicros = 10000; p.monthlyMicros = 10000000;
    int32_t value; assert(effectivePrice(100000,p,value) && value == 249900);
    p.monthlyMicros = 0; assert(effectivePrice(100000,p,value) && value == 249900);
    assert(effectivePrice(-200000,p,value) && value == -107100);
    p.gridMicros = INT32_MAX; assert(!effectivePrice(INT32_MAX,p,value));
    Settings s; s.source = Source::ForecastSolar; s.enabled = true; s.planeCount = 6;
    for (auto& plane : s.planes) { plane.enabled = true; strcpy(plane.name,"explicit plane"); plane.peakW = 1000; }
    JsonDocument config; serializeSettings(s,config.to<JsonObject>()); Settings restored;
    assert(parseSettings(config.as<JsonVariantConst>(),restored) && restored.planeCount == 6);
    config["planes"].add<JsonObject>(); assert(!parseSettings(config.as<JsonVariantConst>(),restored));
    serializeSettings(s,config.to<JsonObject>()); config["planes"][0]["latitude_e6"] = 90000001;
    assert(!parseSettings(config.as<JsonVariantConst>(),restored));
    JsonProvider tariffProvider(Domain::Tariff), forecast(Domain::Forecast); s = {}; s.enabled = true; s.horizonSeconds = 3600;
    tariffProvider.configure(s); forecast.configure(s); a = {}; a.generatedAt = Now - 36000; a.count = 4;
    for (size_t i=0;i<4;++i) { a.intervals[i] = {Now+uint32_t(i)*900,Now+uint32_t(i+1)*900,1000,0,false}; }
    assert(tariffProvider.accept(a,Now,0)); assert(tariffProvider.status(Now,0).planningUsable && tariffProvider.status(Now,0).complete);
    assert(!tariffProvider.status(Now,0).sourceFresh && tariffProvider.status(Now,0).transportFresh);
    assert(tariffProvider.status(Now,3601000).planningUsable && !tariffProvider.status(Now,3601000).transportFresh);
    assert(tariffProvider.status(Now+900,100).partial && tariffProvider.status(Now+900,100).planningUsable);
    tariffProvider.failed("verified_https_failed",100); assert(tariffProvider.status(Now,100).planningUsable && tariffProvider.status(Now,100).error);
    assert(forecast.accept(a,Now,0)); assert(!forecast.status(Now,0).planningUsable && forecast.status(Now,0).sourceAgeSeconds == 36000);
    assert(!forecast.status(Now,0).sourceFresh && forecast.status(Now,0).transportFresh);
    a.generatedAt=Now; assert(forecast.accept(a,Now,0)); forecast.failed("network",100);
    assert(forecast.status(Now,100).sourceFresh && forecast.status(Now,100).transportFresh);
    assert(!forecast.status(Now,3601000).transportFresh && !forecast.status(Now,3601000).planningUsable);
    assert(forecast.status(Now,100).planningUsable); assert(!forecast.status(Now+3601,3601000).planningUsable);
    assert(!forecast.status(Now-1,100).planningUsable); assert(!tariffProvider.status(Now-1,100).planningUsable);
    ++a.generatedAt; assert(tariffProvider.accept(a,Now+1,101)); assert(tariffProvider.data().generatedAt == Now+1);
    a.generatedAt=Now+301; assert(!forecast.accept(a,Now,100));
    assert(tariffProvider.status(Now+900,100).currentCovered); assert(!tariffProvider.status(Now+3600,100).currentCovered);
    std::cout << "Native Energy-Charts v2, Forecast.Solar, DST, aggregation, plane validation, pricing and freshness passed\n";
}
