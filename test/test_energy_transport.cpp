// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Native.h>
#include <hems/profiles/Profile.h>
#include "provider_fixtures.h"
#include <ctime>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace Hems::Energy;
namespace {
bool checkedGet(char const* url, Source source, JsonDocument& doc, NativeInfo& info, char const*& error)
{
    assert(activeClients == 0 && activeHttp == 0);
    bool ok = providerGet(url, source, doc, info, error);
    // Every early return must destroy both clients, including a failed TLS
    // handshake, response rejection, malformed body and JSON allocation failure.
    assert(activeClients == 0 && activeHttp == 0);
    return ok;
}
}
int main()
{
    Hems::Profiles::BoundedAllocator allocator(MaxJsonMemory);
    JsonDocument doc(&allocator); assert(allocator.used == 0); NativeInfo info; char const* error;
    auto get=[&] { return checkedGet("https://api.energy-charts.info/v2/price",Source::EnergyCharts,doc,info,error); };
    transportBody.clear(); serializeJson(ProviderFixtures::prices(2, 1792180800), transportBody);
    assert(get() && caInstalled && redirectPolicy==0 && transportUrl=="https://api.energy-charts.info/v2/price");
    retryAfter = 120; assert(get() && info.retrySeconds == 120); retryAfter = 0; // A 200 response can also impose a server hold.
    assert(!checkedGet("http://api.energy-charts.info/v2/price",Source::EnergyCharts,doc,info,error));
    assert(!checkedGet("https://api.energy-charts.info.attacker.test/",Source::EnergyCharts,doc,info,error));
    assert(!checkedGet("https://api.forecast.solar/",Source::EnergyCharts,doc,info,error));
    beginAvailable = false; assert(!get() && !strcmp(error, "provider_begin_failed")); beginAvailable = true;
    transportCode=-1; assert(!get() && !strcmp(error,"provider_connect_failed")); // TCP/socket and generic TLS -1 remain ambiguous.
    assert(info.httpStatus == -1 && !strcmp(info.httpError, "connection refused") && info.dns == 1);
    dnsAvailable = false; assert(!get() && !strcmp(error, "provider_dns_failed") && info.dns == 0); dnsAvailable = true;
    tlsCode = -0x2700; assert(!get() && !strcmp(error, "provider_tls_error") && info.tlsError == -0x2700 && info.tlsErrorText[0]);
    // Repeated allocator failures on both cloud origins must not retain a client.
    tlsCode = -0x7f00;
    for (unsigned i = 0; i < 10; ++i) {
        for (auto source : { Source::EnergyCharts, Source::ForecastSolar }) {
            auto url = source == Source::EnergyCharts ? "https://api.energy-charts.info/v2/price" : "https://api.forecast.solar/estimate/";
            assert(!checkedGet(url, source, doc, info, error));
            assert(!strcmp(error, "provider_tls_error") && info.tlsError == -0x7f00);
        }
    }
    tlsCode = -1; assert(!get() && !strcmp(error, "provider_connect_failed") && info.tlsError == -1); tlsCode = 0;
    transportCode = -11; assert(!get() && !strcmp(error, "provider_read_timeout"));
    transportCode = -5; assert(!get() && !strcmp(error, "provider_connection_lost"));
    transportCode = -8; assert(!get() && !strcmp(error, "insufficient_memory"));
    transportCode=302; assert(!get() && redirectPolicy==0); transportCode=500; assert(!get());
    transportCode=429; retryAfter=10000; assert(!get() && info.retrySeconds==10000);
    retryAfter=0; assert(!get() && info.retrySeconds == 60);
    assert(!checkedGet("https://api.forecast.solar/estimate/", Source::ForecastSolar, doc, info, error) && info.retrySeconds == 7200);
    time_t tomorrow = time(nullptr) + 86400; struct tm utc {}; gmtime_r(&tomorrow, &utc);
    char dateHeader[64]; strftime(dateHeader, sizeof(dateHeader), "%a, %d %b %Y %H:%M:%S GMT", &utc);
    retryHeader = dateHeader; assert(!get() && info.retrySeconds >= 86395 && info.retrySeconds <= 86400); retryHeader.clear();
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
    transportBody.clear(); serializeJson(ProviderFixtures::prices(2, 1792180800), transportBody);
    for (unsigned i = 0; i < 6; ++i) { assert(get()); assert(activeClients == 0 && activeHttp == 0); }
    assert(tlsHost == "api.energy-charts.info" && caInstalled);
    transportBody = "{\"data\":["; bodyStalled = true; assert(!get() && !strcmp(error,"provider_body_timeout")); bodyStalled = false;
    // Fixture allocations go through the real bounded allocator and production JSON filter.
    for (unsigned count : {192U, 256U}) {
        auto fixture = ProviderFixtures::prices(count, 1792180800U);
        fixture["ignored"] = std::string(8000, 'x');
        transportBody.clear(); serializeJson(fixture, transportBody);
        assert(get() && doc["data"].size() == count && doc["ignored"].isNull());
        Series parsed; assert(parseEnergyCharts(doc.as<JsonVariantConst>(), parsed, error) && parsed.count == count);
        std::cout << "Energy-Charts v2 " << count << " intervals: payload=" << info.payloadBytes << " JSON allocator peak=" << allocator.peak << " host bytes\n";
        doc.clear(); assert(allocator.used == 0);
    }
    auto supplied = ProviderFixtures::load("energy-charts-v2-price.json");
    transportBody.clear(); serializeJson(supplied, transportBody);
    assert(get()); Series parsed; assert(parseEnergyCharts(doc.as<JsonVariantConst>(), parsed, error) && parsed.count == 192);
    // Descriptor-level units survive filtering; an incompatible override must fail.
    supplied["series"][0]["unit"] = "EUR/kWh";
    transportBody.clear(); serializeJson(supplied, transportBody);
    assert(get() && !parseEnergyCharts(doc.as<JsonVariantConst>(), parsed, error));
    doc.clear();
    JsonDocument fixture; fixture["message"]["code"] = 0; fixture["message"]["type"] = "success";
    fixture["message"]["info"]["time_utc"] = "2026-10-17T00:00:00Z";
    for (unsigned i = 0; i <= MaxIntervals; ++i) {
        time_t t = 1792180800U + i * 900; struct tm utc {}; gmtime_r(&t, &utc);
        char key[32]; strftime(key, sizeof(key), "%Y-%m-%d %H:%M:%S", &utc);
        fixture["result"]["watt_hours_period"][key] = i ? 250 : 0;
    }
    transportBody.clear(); serializeJson(fixture, transportBody);
    assert(checkedGet("https://api.forecast.solar/estimate/", Source::ForecastSolar, doc, info, error));
    assert(doc["result"]["watt_hours_period"].size() == MaxIntervals + 1 && activeClients == 0 && activeHttp == 0);
    std::cout << "Forecast.Solar 256 intervals: payload=" << info.payloadBytes << " JSON allocator peak=" << allocator.peak << " host bytes\n";
    Hems::Profiles::BoundedAllocator tiny(1); JsonDocument noMemory(&tiny);
    assert(!checkedGet("https://api.forecast.solar/estimate/", Source::ForecastSolar, noMemory, info, error) && !strcmp(error, "insufficient_memory"));
    std::cout<<"Production verified HTTPS transport origin, CA wiring, redirects, failure/recovery, retry and bounded-body tests passed\n";
}
