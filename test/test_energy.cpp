// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Provider.h>
#include <hems/profiles/Profile.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace Hems::Energy;
static constexpr uint32_t Now = 1792180800;
JsonDocument fixture(Domain domain, unsigned count = 4, uint32_t start = Now)
{
    JsonDocument doc;
    doc["schema"] = schema(domain); doc["source"] = "fixture"; doc["generated_at"] = Now;
    doc["currency"] = "EUR";
    for (unsigned i = 0; i < count; ++i) {
        auto interval = doc["intervals"].add<JsonObject>();
        interval["start"] = start + i * 900; interval["end"] = start + (i + 1) * 900;
        if (domain == Domain::Tariff) {
            interval["price_micros_per_kwh"] = i ? 250000 : -50000;
            if (i) { interval["import_price_micros_per_kwh"] = 300000; }
        } else { interval["power_w"] = i * 1000; }
    }
    return doc;
}
int main()
{
    Settings settings; settings.enabled = true; settings.horizonSeconds = 3600;
    JsonDocument config; serializeSettings(settings, config.to<JsonObject>());
    Settings restored; assert(parseSettings(config.as<JsonVariantConst>(), restored));
    assert(restored.enabled && restored.horizonSeconds == 3600);
    for (auto url : { "https://bridge/data", "http://user:secret@bridge", "ftp://bridge", "http://", "http://a b" }) {
        config["url"] = url; assert(!parseSettings(config.as<JsonVariantConst>(), restored));
    }
    config["url"] = "http://bridge.local/data"; assert(parseSettings(config.as<JsonVariantConst>(), restored));
    config["refresh_seconds"] = 59; assert(!parseSettings(config.as<JsonVariantConst>(), restored));
    for (auto domain : { Domain::Tariff, Domain::Forecast }) {
        auto doc = fixture(domain); Series series; char const* error;
        assert(parseSeries(doc.as<JsonVariantConst>(), domain, series, error));
        assert(series.count == 4 && !error);
        assert(series.current(Now) == &series.intervals[0]);
        assert(series.current(Now + 900) == &series.intervals[1]);
        assert(!series.current(Now - 1) && !series.current(Now + 3600));
        assert(series.covers(Now + 1, Now + 3600));
        assert(!series.covers(Now - 1, Now + 100) && !series.covers(Now, Now + 3601));
        assert(!series.covers(Now, Now));
        if (domain == Domain::Tariff) {
            assert(series.intervals[0].value == -50000 && !series.intervals[0].hasImportPrice);
            assert(series.intervals[1].importPrice == 300000 && series.intervals[1].hasImportPrice);
        } else {
            int64_t energy;
            assert(series.energyWattSeconds(Now, Now + 3600, energy) && energy == 1500 * 3600);
            assert(series.energyWattSeconds(Now + 1350, Now + 2250, energy) && energy == 1500 * 900);
            assert(!series.energyWattSeconds(Now, Now + 3601, energy) && energy == 0);
        }
        JsonProvider provider(domain); Provider const& contract = provider;
        assert(!contract.status(Now, 0).enabled);
        provider.configure(settings);
        assert(!contract.status(Now, 0).hasData);
        assert(!provider.accept(series, 0, 0));
        assert(provider.accept(series, Now, 0));
        assert(contract.status(Now, 0).fresh && contract.status(Now, 0).complete);
        assert(!strcmp(contract.status(Now, 0).state, "ready"));
        provider.failed("network_unavailable", 10);
        assert(contract.status(Now, 10).fresh && contract.status(Now, 10).error);
        assert(provider.data().count == 4);
        assert(provider.accept(series, Now, 20));
        assert(!contract.status(Now, 20).error);
        assert(contract.status(Now, 3601021).fresh == false); // fake monotonic clock, wall clock frozen
        assert(!contract.status(Now + 3601, 21).fresh);
        assert(!contract.status(Now - 1, 21).fresh); // wall clock stepped backwards
        assert(!contract.status(0, 21).fresh);
        auto older = series; --older.generatedAt;
        assert(!provider.accept(older, Now, 22));
        assert(provider.data().generatedAt == Now);
        auto future = series; future.generatedAt = Now + 301;
        assert(!provider.accept(future, Now, 23));
        series.intervals[1].start++;
        assert(provider.accept(series, Now, 24));
        assert(contract.status(Now, 24).fresh && !contract.status(Now, 24).complete);
        assert(!series.current(Now + 900));
        series.generatedAt = Now - 5000;
        JsonProvider stale(domain); stale.configure(settings);
        assert(stale.accept(series, Now, 100)); // diagnostics remain visible, never freshened by fetching old source data
        assert(!stale.status(Now, 100).fresh);
        settings.enabled = false; provider.configure(settings);
        assert(!contract.status(Now, 30).hasData && !contract.status(Now, 30).fresh);
        settings.enabled = true;
        strcpy(settings.url, "http://bridge/data"); provider.configure(settings);
        assert(provider.refreshDue(0)); provider.attempted(0);
        assert(!provider.refreshDue(899999) && provider.refreshDue(900000));
        provider.attempted(0xfffffff0); assert(provider.refreshDue(uint32_t(0xfffffff0 + 900000)));
        settings.url[0] = 0;

        // Rejection is atomic; malformed payloads cannot partially replace normalized data.
        doc = fixture(domain); Series unchanged; unchanged.count = 7;
        doc["intervals"][1]["start"] = Now; // duplicate/overlap
        assert(!parseSeries(doc.as<JsonVariantConst>(), domain, unchanged, error) && unchanged.count == 7);
        doc = fixture(domain); doc["intervals"][0]["end"] = Now;
        assert(!parseSeries(doc.as<JsonVariantConst>(), domain, unchanged, error));
        for (auto bad : { "\"1\"", "null", "true", "1.5", "2147483648" }) {
            doc = fixture(domain); JsonDocument value; assert(!deserializeJson(value, bad));
            doc["intervals"][0][domain == Domain::Tariff ? "price_micros_per_kwh" : "power_w"] = value;
            assert(!parseSeries(doc.as<JsonVariantConst>(), domain, unchanged, error));
        }
        doc = fixture(domain, MaxIntervals + 1);
        assert(!parseSeries(doc.as<JsonVariantConst>(), domain, unchanged, error));
        doc = fixture(domain, MaxIntervals);
        std::string wire; serializeJson(doc, wire);
        assert(wire.size() < MaxPayload);
        Hems::Profiles::BoundedAllocator allocator(MaxJsonMemory); JsonDocument bounded(&allocator);
        assert(!deserializeJson(bounded, wire));
        assert(parseSeries(bounded.as<JsonVariantConst>(), domain, unchanged, error));
        assert(allocator.peak <= MaxJsonMemory);
        std::cout << (domain == Domain::Tariff ? "Tariff" : "Forecast") << " JSON peak native bytes: " << allocator.peak << '\n';
    }
    // DST belongs to the bridge: UTC intervals naturally support 23/25-hour local days.
    for (unsigned periods : { 92, 100 }) {
        auto doc = fixture(Domain::Tariff, periods); Series day; char const* error;
        assert(parseSeries(doc.as<JsonVariantConst>(), Domain::Tariff, day, error));
        assert(day.covers(Now, Now + periods * 900));
        for (unsigned i = 1; i < periods; ++i) { assert(day.intervals[i].start == day.intervals[i - 1].end); }
    }
    std::cout << "Energy domain lookup, units, gaps, UTC/DST, bounded parsing, freshness and recovery passed; series bytes=" << sizeof(Series) << '\n';
}
