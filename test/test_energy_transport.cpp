// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <hems/profiles/Profile.h>
#include <ctime>
#include <WiFiClientSecure.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace Hems::Energy;
namespace {
void addPriceRow(JsonArray data, uint32_t timestamp, double value)
{
    time_t t = timestamp; struct tm utc {}; gmtime_r(&t, &utc);
    char text[32]; strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    auto row = data.add<JsonObject>(); row["timestamp"] = text; row["values"]["day_ahead_price"] = value;
}
void makePriceFixture(JsonDocument& fixture, unsigned count)
{
    fixture.clear();
    fixture["schema_version"] = "2.0"; fixture["endpoint"] = "price"; fixture["bidding_zone"] = "DE-LU";
    fixture["timezone"] = "Europe/Berlin"; fixture["interval_minutes"] = 15; fixture["unit"] = "EUR/MWh";
    fixture["generated_at"] = "2026-10-16T19:59:00+00:00"; fixture["deprecated"] = false;
    auto series = fixture["series"].add<JsonObject>(); series["id"] = "day_ahead_price"; series["name"] = "Day-ahead spot market price";
    auto data = fixture["data"].to<JsonArray>();
    for (unsigned i = 0; i < count; ++i) { addPriceRow(data, 1792180800U + i * 900, 123.456); }
}
}
int main()
{
    Hems::Profiles::BoundedAllocator allocator(MaxJsonMemory);
    JsonDocument doc(&allocator); assert(allocator.used == 0); NativeInfo info; char const* error;
    auto get=[&] { return providerGet("https://api.energy-charts.info/v2/price",Source::EnergyCharts,doc,info,error); };
    JsonDocument base; makePriceFixture(base, 2); transportBody.clear(); serializeJson(base, transportBody);
    assert(get() && caInstalled && redirectPolicy==0 && transportUrl=="https://api.energy-charts.info/v2/price");
    retryAfter = 120; assert(get() && info.retrySeconds == 120); retryAfter = 0; // A 200 response can also impose a server hold.
    assert(!providerGet("http://api.energy-charts.info/v2/price",Source::EnergyCharts,doc,info,error));
    assert(!providerGet("https://api.energy-charts.info.attacker.test/v2/price",Source::EnergyCharts,doc,info,error));
    assert(!providerGet("https://api.forecast.solar/",Source::EnergyCharts,doc,info,error));
    transportCode=-1; assert(!get() && !strcmp(error,"provider_connect_failed")); // TCP/socket and generic TLS -1 remain ambiguous.
    assert(info.httpStatus == -1 && !strcmp(info.httpError, "connection refused") && info.dns == 1);
    dnsAvailable = false; assert(!get() && !strcmp(error, "provider_dns_failed") && info.dns == 0); dnsAvailable = true;
    tlsCode = -0x2700; assert(!get() && !strcmp(error, "provider_tls_error") && info.tlsError == -0x2700 && info.tlsErrorText[0]);
    tlsCode = -1; assert(!get() && !strcmp(error, "provider_connect_failed") && info.tlsError == -1); tlsCode = 0;
    transportCode = -11; assert(!get() && !strcmp(error, "provider_read_timeout"));
    transportCode = -5; assert(!get() && !strcmp(error, "provider_connection_lost"));
    transportCode = -8; assert(!get() && !strcmp(error, "insufficient_memory"));
    transportCode=302; assert(!get() && redirectPolicy==0); transportCode=500; assert(!get());
    transportCode=429; retryAfter=10000; assert(!get() && info.retrySeconds==10000);
    transportCode=503; retryAfter=0; assert(!get() && info.retrySeconds>=900);
    transportCode=200; transportBody="{broken"; assert(!get());
    transportBody="{} trailing"; assert(!get());
    // A complete HTTP body with incomplete JSON does not prove a lost connection.
    transportBody="{\"data\":["; contentLength=transportBody.size();
    assert(!get() && !strcmp(error,"malformed_or_oversized_json"));
    contentLength=-1; assert(!get() && !strcmp(error,"malformed_or_oversized_json"));
    transportBody="{}"; contentLength=MaxPayload+1; assert(!get());
    contentLength=5; assert(!get()); // Truncated Content-Length must not accept a complete JSON prefix.
    contentLength=MaxPayload; transportBody="{}"+std::string(MaxPayload-2,' '); assert(get());
    contentLength=-1; transportBody="{}"+std::string(MaxPayload,' '); assert(!get());
    makePriceFixture(base, 2); transportBody.clear(); serializeJson(base, transportBody);
    for (unsigned i = 0; i < 6; ++i) { assert(get()); assert(activeClients == 0 && activeHttp == 0); }
    assert(tlsHost == "api.energy-charts.info" && caInstalled);
    transportBody = "{\"data\":["; bodyStalled = true; assert(!get() && !strcmp(error,"provider_body_timeout")); bodyStalled = false;
    // Fixture allocations go through the real bounded allocator and production JSON filter.
    for (unsigned count : {192U, 256U}) {
        JsonDocument fixture; makePriceFixture(fixture, count); fixture["ignored"] = std::string(8000, 'x');
        transportBody.clear(); serializeJson(fixture, transportBody);
        assert(transportBody.size() <= MaxPayload);
        assert(get() && doc["data"].size() == count && doc["ignored"].isNull());
        assert(doc["series"].size() == 1 && doc["generated_at"].is<const char*>());
        std::cout << "Energy-Charts v2 " << count << " intervals: payload=" << info.payloadBytes << " JSON allocator peak=" << allocator.peak << " host bytes\n";
        doc.clear(); assert(allocator.used == 0);
    }
    JsonDocument fixture; fixture["message"]["code"] = 0; fixture["message"]["type"] = "success";
    fixture["message"]["info"]["time_utc"] = "2026-10-17T00:00:00Z";
    for (unsigned i = 0; i <= MaxIntervals; ++i) {
        time_t t = 1792180800U + i * 900; struct tm utc {}; gmtime_r(&t, &utc);
        char key[32]; strftime(key, sizeof(key), "%Y-%m-%d %H:%M:%S", &utc);
        fixture["result"]["watt_hours_period"][key] = i ? 250 : 0;
    }
    transportBody.clear(); serializeJson(fixture, transportBody);
    assert(providerGet("https://api.forecast.solar/estimate/", Source::ForecastSolar, doc, info, error));
    assert(doc["result"]["watt_hours_period"].size() == MaxIntervals + 1 && activeClients == 0 && activeHttp == 0);
    std::cout << "Forecast.Solar 256 intervals: payload=" << info.payloadBytes << " JSON allocator peak=" << allocator.peak << " host bytes\n";
    Hems::Profiles::BoundedAllocator tiny(1); JsonDocument noMemory(&tiny);
    assert(!providerGet("https://api.forecast.solar/estimate/", Source::ForecastSolar, noMemory, info, error) && !strcmp(error, "insufficient_memory"));
    std::cout<<"Production verified HTTPS transport origin, CA wiring, redirects, failure/recovery, retry and bounded-body tests passed\n";
}
