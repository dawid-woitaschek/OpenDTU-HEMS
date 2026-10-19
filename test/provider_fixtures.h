// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio>
#include <ctime>
#include <functional>

namespace ProviderFixtures {
inline JsonDocument load(char const* name)
{
    std::ifstream input(std::string("fixtures/providers/") + name);
    if (!input) { input.open(std::string("test/fixtures/providers/") + name); }
    assert(input);
    JsonDocument doc; assert(!deserializeJson(doc, input)); return doc;
}
inline std::string timestamp(uint32_t utc, unsigned offset = 7200)
{
    time_t local = static_cast<uint64_t>(utc) + offset; struct tm date {}; gmtime_r(&local, &date);
    char text[32], zone[16]; strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &date);
    snprintf(zone, sizeof(zone), "+%02u:%02u", offset / 3600, offset % 3600 / 60);
    return std::string(text) + zone;
}
inline JsonDocument prices(unsigned count, uint32_t first, unsigned resolution = 900,
    std::function<unsigned(uint32_t)> offset = [](uint32_t) { return 7200U; })
{
    JsonDocument doc;
    doc["schema_version"] = "2.0"; doc["endpoint"] = "price"; doc["bidding_zone"] = "DE-LU";
    doc["country"] = "de"; doc["timezone"] = "Europe/Berlin"; doc["unit"] = "EUR / MWh";
    doc["resolution"] = resolution == 900 ? "PT15M" : "PT1H"; doc["interval_minutes"] = resolution / 60;
    doc["generated_at"] = timestamp(first, offset(first));
    doc["available_from"] = timestamp(first, offset(first));
    auto last = first + (count ? count - 1 : 0) * resolution;
    doc["available_until"] = timestamp(last, offset(last));
    doc["deprecated"] = false; doc["license"] = "CC BY 4.0 from Bundesnetzagentur | SMARD.de";
    auto descriptor = doc["series"].add<JsonObject>(); descriptor["id"] = "day_ahead_price"; descriptor["name"] = "Day-ahead price";
    for (unsigned i = 0; i < count; ++i) {
        auto start = first + i * resolution; auto row = doc["data"].add<JsonObject>();
        row["timestamp"] = timestamp(start, offset(start)); row["values"]["day_ahead_price"] = i ? 123.456 : -55.5;
    }
    return doc;
}
} // namespace ProviderFixtures
