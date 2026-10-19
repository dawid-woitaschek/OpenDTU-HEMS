// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include "provider_fixtures.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
using namespace Hems::Energy;
constexpr uint32_t First = 1792360800; // 2026-10-19 00:00 Europe/Berlin.

int main()
{
    Series output; char const* error = nullptr;
    auto parse = [&](JsonDocument const& doc) { return parseEnergyCharts(doc.as<JsonVariantConst>(), output, error); };
    auto live = ProviderFixtures::load("energy-charts-v2-price.json");
    assert(parse(live) && output.count == 192 && output.sourceTimestampKnown);
    uint32_t generated; assert(parseUtc("2026-10-19T21:05:20+02:00", generated));
    assert(output.generatedAt == generated && output.intervals[0].start == First);
    assert(output.intervals[0].value == 187790 && output.intervals[191].value == 185430);
    assert(output.covers(First, First + 48 * 3600));
    auto accepted = output;
    auto rejected = [&](auto mutate) {
        auto doc = ProviderFixtures::prices(8, First); mutate(doc);
        assert(!parse(doc) && !memcmp(&accepted, &output, sizeof(output)));
    };
    rejected([](JsonDocument& d) { d["schema_version"] = "1.0"; });
    rejected([](JsonDocument& d) { d["endpoint"] = "price_current"; });
    rejected([](JsonDocument& d) { d["bidding_zone"] = "AT"; });
    rejected([](JsonDocument& d) { d["country"] = "at"; });
    rejected([](JsonDocument& d) { d["timezone"] = "UTC"; });
    rejected([](JsonDocument& d) { d["unit"] = "EUR/kWh"; });
    rejected([](JsonDocument& d) { d["interval_minutes"] = 60; });
    rejected([](JsonDocument& d) { d["interval_minutes"] = "15"; });
    rejected([](JsonDocument& d) { d["resolution"] = "PT30M"; });
    rejected([](JsonDocument& d) { d["resolution"] = nullptr; });
    rejected([](JsonDocument& d) { d["series"][0]["id"] = "unrelated"; });
    rejected([](JsonDocument& d) { d["series"].add<JsonObject>()["id"] = "day_ahead_price"; });
    rejected([](JsonDocument& d) { d["series"][0]["unit"] = "EUR/kWh"; });
    rejected([](JsonDocument& d) { d["series"] = nullptr; });
    rejected([](JsonDocument& d) { d["license"] = 7; });
    rejected([](JsonDocument& d) { d["generated_at"] = "invalid"; });
    rejected([](JsonDocument& d) { d["generated_at"] = "x"; });
    rejected([](JsonDocument& d) { d["generated_at"] = "2026-10-19T00:00:00+01:00"; }); // Wrong Berlin offset.
    rejected([](JsonDocument& d) { d["data"][1]["timestamp"] = "2026-02-30T00:00:00+01:00"; });
    rejected([](JsonDocument& d) { d["data"][1]["timestamp"] = "2026-10-19T00:15:00.5+02:00"; });
    rejected([](JsonDocument& d) { d["data"][1]["timestamp"] = ProviderFixtures::timestamp(First - 900); });
    rejected([](JsonDocument& d) { d["data"][1]["timestamp"] = ProviderFixtures::timestamp(First); });
    rejected([](JsonDocument& d) { d["data"][1]["timestamp"] = ProviderFixtures::timestamp(First + 1800); });
    rejected([](JsonDocument& d) { d["data"][1]["values"].remove("day_ahead_price"); });
    rejected([](JsonDocument& d) { d["data"][1]["values"]["day_ahead_price"] = true; });
    rejected([](JsonDocument& d) { d["data"][1]["values"]["day_ahead_price"] = "1.2"; });
    rejected([](JsonDocument& d) { d["data"][1]["values"]["day_ahead_price"] = 1e30; });
    rejected([](JsonDocument& d) { d["data"][1]["values"]["day_ahead_price"] = -2147483.649; });
    rejected([](JsonDocument& d) { d["data"][1]["values"]["day_ahead_price"] = std::numeric_limits<double>::infinity(); });
    rejected([](JsonDocument& d) { d["available_from"] = ProviderFixtures::timestamp(First + 900); });
    rejected([](JsonDocument& d) { d["available_until"] = ProviderFixtures::timestamp(First + 6 * 900); });
    rejected([](JsonDocument& d) { d["available_until"] = "invalid"; });
    rejected([](JsonDocument& d) { d.remove("available_from"); });
    rejected([](JsonDocument& d) { d.remove("deprecated"); });
    rejected([](JsonDocument& d) { d["deprecated"] = "false"; });
    rejected([](JsonDocument& d) { d["deprecated"] = true; });
    assert(!strcmp(error, "energy_charts_deprecated"));
    auto excessive = ProviderFixtures::prices(257, First); assert(!parse(excessive) && !strcmp(error, "invalid_interval_count"));
    JsonDocument legacy; legacy["unix_seconds"].add(First); legacy["price"].add(100); legacy["unit"] = "EUR / MWh";
    assert(!parse(legacy) && !memcmp(&accepted, &output, sizeof(output))); // No hybrid parser or v1 fallback.

    auto doc = ProviderFixtures::prices(8, First);
    doc["series"][0]["id"] = "additional_series";
    auto descriptor = doc["series"].add<JsonObject>(); descriptor["id"] = "day_ahead_price"; descriptor["unit"] = "EUR / MWh";
    doc["generated_at"] = "2026-10-19T00:00:00.123456+02:00";
    assert(parse(doc) && output.generatedAt == First); // Stable ID lookup; second-precision generation metadata.
    double values[] = { -55.5, -0.0005, 0.0005, 123.456, 2147483.647, -2147483.648, 0, -1.234 };
    int32_t micros[] = { -55500, -1, 1, 123456, INT32_MAX, INT32_MIN, 0, -1234 };
    for (unsigned i = 0; i < 8; ++i) { doc["data"][i]["values"]["day_ahead_price"] = values[i]; }
    assert(parse(doc)); for (unsigned i = 0; i < 8; ++i) { assert(output.intervals[i].value == micros[i]); }

    doc = ProviderFixtures::prices(48, First, 3600);
    assert(parse(doc) && output.count == 48 && output.covers(First, First + 48 * 3600));
    for (unsigned i = 0; i < output.count; ++i) { assert(output.intervals[i].end - output.intervals[i].start == 3600); }
    doc["interval_minutes"] = 15; doc["resolution"] = "PT15M"; assert(!parse(doc));
    doc = ProviderFixtures::prices(1, First); assert(parse(doc) && output.count == 1 && output.intervals[0].end == First + 900);

    auto springOffset = [](uint32_t t) { return t < 1774746000 ? 3600U : 7200U; };
    auto autumnOffset = [](uint32_t t) { return t < 1792890000 ? 7200U : 3600U; };
    for (bool spring : {true, false}) {
        uint32_t start = spring ? 1774738800 : 1792879200; unsigned hours = spring ? 23 : 25;
        doc = ProviderFixtures::prices((hours + 24) * 4, start, 900, spring ? +springOffset : +autumnOffset);
        assert(parse(doc) && output.count == (hours + 24) * 4 && output.covers(start, start + (hours + 24) * 3600));
        uint32_t dayStart, dayEnd; germanyDay(start + 12 * 3600, 0, dayStart, dayEnd);
        assert(dayStart == start && dayEnd - dayStart == hours * 3600);
        char url[256]; tariffUrl(start + 12 * 3600, url, sizeof(url));
        auto expected = std::string("https://api.energy-charts.info/v2/price?bzn=DE-LU&start=")
            + std::to_string(start) + "&end=" + std::to_string(start + (hours + 24) * 3600 - 1);
        assert(url == expected);
    }

    doc = ProviderFixtures::prices(192, First);
    for (unsigned i = 108; i < 192; ++i) { doc["data"][i]["values"]["day_ahead_price"] = nullptr; }
    doc["available_until"] = ProviderFixtures::timestamp(First + 107 * 900);
    assert(parse(doc) && output.count == 108 && output.covers(First, First + 27 * 3600));
    assert(!output.current(First + 27 * 3600)); // available_until labels the last interval START.
    Settings settings; settings.enabled = true; settings.source = Source::EnergyCharts;
    settings.horizonSeconds = 48 * 3600; settings.maxAgeSeconds = 60;
    JsonProvider provider(Domain::Tariff); provider.configure(settings);
    uint32_t fetched = First + 3600; assert(provider.accept(output, fetched, 0));
    auto status = provider.status(fetched, 0);
    assert(status.currentCovered && status.planningUsable && status.partial && !status.complete);
    assert(status.sourceAt == First && status.fetchedAt == fetched && status.sourceAgeSeconds == 3600);
    assert(!status.sourceFresh && status.transportFresh); // Generation age is not market-data expiry.
    accepted = provider.data(); provider.failed("provider_rate_limited", 1000); provider.defer(1000, 120, RetryKind::RateLimited);
    assert(!memcmp(&accepted, &provider.data(), sizeof(accepted)));
    assert(provider.status(fetched, 1000).planningUsable && provider.status(fetched, 1000).error);
    assert(provider.retryDecision(1000).retryAfterSeconds == 120 && !provider.retryAllowed(120999));
    assert(provider.retryAllowed(121000));
    assert(provider.status(First + 26 * 3600, 7200000).planningUsable); // Retained covered prices remain usable.
    assert(!provider.status(First + 27 * 3600, 7200000).planningUsable); // Uncovered/expired range is unusable.
    doc["data"][8]["values"]["day_ahead_price"] = nullptr; assert(parse(doc));
    assert(provider.accept(output, fetched, 2000) && !provider.status(First + 8 * 900, 2000).planningUsable);
    assert(!output.covers(First, First + 27 * 3600));
    for (auto row : doc["data"].as<JsonArray>()) { row["values"]["day_ahead_price"] = nullptr; }
    doc["available_from"] = nullptr; doc["available_until"] = nullptr;
    accepted = output; assert(!parse(doc) && !strcmp(error, "prices_not_published"));
    assert(!memcmp(&accepted, &output, sizeof(output)));
    std::cout << "Strict v2 envelope, source/fetch semantics, exact money, null/partial coverage, DST, hourly, rejection atomicity and retention passed\n";
}
