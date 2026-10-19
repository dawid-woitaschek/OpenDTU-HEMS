// SPDX-License-Identifier: GPL-2.0-or-later
#include "energy_harness.h"
#include "provider_fixtures.h"
void rejected(Service& s, Domain domain, char const* reason, uint32_t wait = 0)
{
    auto result = s.requestRefresh(domain);
    assert(!result && !strcmp(result.reason, reason) && result.retryAfterSeconds == wait);
}
void fixture()
{
    auto doc = ProviderFixtures::prices(8, currentUtc);
    httpBody.clear(); serializeJson(doc, httpBody);
}
int main()
{
    Service service; Scheduler scheduler; service.init(scheduler);
    rejected(service, Domain::Tariff, "not_enabled");
    Settings settings; settings.enabled = true; settings.source = Source::EnergyCharts;
    assert(service.configure(Domain::Tariff, settings));
    currentUtc = 0; rejected(service, Domain::Tariff, "clock_invalid"); currentUtc = 1792180800;

    // A formally plausible wall clock is not sufficient for native HTTPS. Wait
    // for MycilaNTP's durable first-sync latch, then retry locally after 5 s.
    NtpSettings.synced = false;
    rejected(service, Domain::Tariff, "clock_invalid");
    scheduler.execute(); runWorkers(); assert(tasks.empty() && httpRequests == 0);
    Series data; Status status; service.snapshot(Domain::Tariff, data, status);
    assert(!strcmp(status.error, "clock_invalid") && status.nextRefresh == currentUtc + 5);

    NtpSettings.synced = true; NetworkSettings.connected = false; testNow += 5000;
    scheduler.execute(); runWorkers(); assert(tasks.empty() && httpRequests == 0);
    service.snapshot(Domain::Tariff, data, status);
    assert(!strcmp(status.error, "network_unavailable") && status.nextRefresh == currentUtc + 5);
    JsonDocument api; service.serialize(Domain::Tariff, api.to<JsonObject>());
    assert(api["request"]["http_status"] == 0 && api["request"]["dns"] == "not_attempted");
    assert(api["request"]["before"]["free_internal_bytes"] == 0);
    NetworkSettings.connected = true; fixture();
    rejected(service, Domain::Tariff, "local_retry", 5);
    testNow += 4999; scheduler.execute(); runWorkers(); assert(httpRequests == 0);
    ++testNow; scheduler.execute(); runWorkers(); assert(httpRequests == 1 && tasks.size() == 1);
    service.snapshot(Domain::Tariff, data, status); assert(status.hasData && !status.error && status.nextRefresh == currentUtc + 3600);
    rejected(service, Domain::Tariff, "transport_backoff", 30);
    testNow += 30000; assert(service.requestRefresh(Domain::Tariff)); // Bypasses the 1-hour auto interval.
    rejected(service, Domain::Tariff, "busy");
    duringHttp = [&] { rejected(service, Domain::Tariff, "busy"); scheduler.execute(); }; // Cannot enqueue a duplicate while running.
    scheduler.execute(); runWorkers(); assert(httpRequests == 2);
    scheduler.execute(); runWorkers(); assert(httpRequests == 2);
    // DNS/transport failure uses bounded exponential retry, not ordinary cadence.
    testNow += 30000; httpAvailable = false; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    rejected(service, Domain::Tariff, "transport_backoff", 30);
    auto before = httpRequests; for (unsigned i = 0; i < 5; ++i) { scheduler.execute(); runWorkers(); rejected(service, Domain::Tariff, "transport_backoff", 30); }
    assert(httpRequests == before);
    testNow += 30000; scheduler.execute(); runWorkers(); rejected(service, Domain::Tariff, "transport_backoff", 30);
    testNow += 30000; httpAvailable = true; scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.hasData && !status.error && status.nextRefresh == currentUtc + 3600);
    testNow += 30000; httpAvailable = false;
    assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    for (auto delay : {30U, 30U, 60U, 120U, 240U, 300U, 300U}) {
        rejected(service, Domain::Tariff, "transport_backoff", delay);
        testNow += delay * 1000; scheduler.execute(); runWorkers();
    }
    httpAvailable = true; testNow += 300000; scheduler.execute(); runWorkers();
    // Server/quota holds are independent and cannot be shortened by local failure, success, or same-provider config changes.
    JsonProvider provider(Domain::Tariff); provider.configure(settings);
    provider.defer(0, 900); provider.failed("network_unavailable", 1000);
    assert(!provider.retryAllowed(5000) && !strcmp(provider.retryDecision(5000).reason, "provider_backoff"));
    provider.defer(5000, 10); provider.configure(settings); assert(!provider.retryAllowed(899999)); assert(provider.retryAllowed(900000));
    provider.defer(900000, 7200, RetryKind::RateLimited); provider.defer(900000, 300);
    assert(!strcmp(provider.retryDecision(900000).reason, "rate_limited"));
    // Exercise a successful HTTP Retry-After through the production Service path.
    testNow += 30000; nativeRetrySeconds = 120; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    rejected(service, Domain::Tariff, "provider_backoff", 120);
    testNow += 119000; rejected(service, Domain::Tariff, "provider_backoff", 1);
    testNow += 1000; nativeRetrySeconds = 0;
    // Failed HTTP rate limit is equally binding. Config generation changes discard
    // the result, but cannot erase a Retry-After from a request already made.
    nativeHttpStatus = 429; nativeRetrySeconds = 10000;
    assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    rejected(service, Domain::Tariff, "rate_limited", 10000);
    auto changed = settings; changed.refreshSeconds = 4000;
    assert(service.configure(Domain::Tariff, changed));
    rejected(service, Domain::Tariff, "rate_limited", 10000);
    testNow += 10000000; nativeHttpStatus = 200; nativeRetrySeconds = 120;
    duringHttp = [&] { changed.refreshSeconds = 5000; assert(service.configure(Domain::Tariff, changed)); };
    assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    rejected(service, Domain::Tariff, "provider_backoff", 120);
    testNow += 120000; nativeRetrySeconds = 0;
    // Keep the two providers' deterministic response fixtures independent.
    changed.enabled = false; assert(service.configure(Domain::Tariff, changed));
    // Two-hour solar ordinary interval is also bypassed manually after short spacing.
    settings.source = Source::ForecastSolar; settings.planeCount = 1; settings.planes[0].enabled = true;
    strcpy(settings.planes[0].name, "PV"); settings.planes[0].peakW = 1000;
    assert(service.configure(Domain::Forecast, settings));
    httpBody = R"({"message":{"code":0,"type":"success","info":{"time_utc":"2026-10-16T20:00:00Z"}},"result":{"watt_hours_period":{"2026-10-16 20:00:00":0,"2026-10-16 21:00:00":1000}}})";
    NetworkSettings.connected = false;
    auto requests = solarRequests; scheduler.execute(); runWorkers(); assert(solarRequests == requests);
    service.snapshot(Domain::Forecast, data, status); assert(status.nextRefresh == currentUtc + 5);
    NetworkSettings.connected = true; testNow += 5000; scheduler.execute(); runWorkers(); assert(solarRequests == requests + 1);
    testNow += 15000; assert(service.requestRefresh(Domain::Forecast)); scheduler.execute(); runWorkers();
    // Invalid polling configuration and unavailable network remain explicit manual rejections.
    settings = {}; settings.enabled = true; assert(service.configure(Domain::Forecast, settings));
    rejected(service, Domain::Forecast, "invalid_config");
    for (auto task : tasks) { delete task; }
    std::cout << "Production NTP/network preflight, busy/manual reasons, auto cadence, transport backoff and server/quota holds passed\n";
}