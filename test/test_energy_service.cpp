#include "energy_harness.h"
int main()
{
    Service service; Scheduler scheduler; service.init(scheduler);
    scheduler.execute(); assert(tasks.empty()); // Disabled/push-only operation allocates no worker.
    Settings settings; settings.enabled = true; settings.refreshSeconds = 60;
    assert(service.configure(Domain::Tariff, settings));
    auto wire = payload(Domain::Tariff); JsonDocument doc; assert(!deserializeJson(doc, wire));
    assert(service.ingest(Domain::Tariff, doc.as<JsonVariantConst>()));
    Series data; Status status; service.snapshot(Domain::Tariff, data, status);
    assert(data.count == 1 && status.fresh && status.complete);
    JsonDocument api; service.serialize(Domain::Tariff, api.to<JsonObject>());
    assert(api["current"]["price_micros_per_kwh"] == 500 && api["fresh"] == true);
    assert(api["current"]["import_price_micros_per_kwh"].isNull());
    assert(api["source_fresh"] == true && api["transport_fresh"] == true);
    assert(!service.requestRefresh(Domain::Tariff));
    scheduler.execute(); assert(tasks.empty());
    failCommit = true; auto disabled = settings; disabled.enabled = false;
    assert(!service.configure(Domain::Tariff, disabled));
    failCommit = false;
    service.snapshot(Domain::Tariff, data, status); assert(status.enabled && status.fresh);
    // Reload real serialized settings through the same production init path; interval data is volatile.
    Service reboot; Scheduler rebootScheduler; reboot.init(rebootScheduler);
    reboot.snapshot(Domain::Tariff, data, status); assert(status.enabled && !status.hasData);
    strcpy(settings.url, "http://bridge.local/data"); assert(service.configure(Domain::Tariff, settings));
    httpBody = wire; scheduler.execute(); assert(tasks.size() == 1); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.fresh && status.hasData);
    auto count = httpRequests; scheduler.execute(); runWorkers(); assert(httpRequests == count);
    testNow += 60000; httpAvailable = false; scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.hasData && status.error && status.fresh);
    httpAvailable = true; testNow += 15000; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.fresh && !status.error);
    httpBody = "{broken"; testNow += 15000; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.hasData && status.error);
    httpBody = wire;
    // Late HTTP response must not populate a disabled or replaced configuration generation.
    duringHttp = [&] { assert(service.configure(Domain::Tariff, disabled)); };
    testNow += 15000; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(!status.enabled && !status.hasData);
    // Forecast uses the same worker; a disabled tariff never affects it.
    assert(service.configure(Domain::Forecast, settings)); httpBody = payload(Domain::Forecast);
    scheduler.execute(); runWorkers(); assert(tasks.size() == 1);
    service.snapshot(Domain::Forecast, data, status); assert(status.fresh && data.intervals[0].value == 500);
    service.serialize(Domain::Forecast, api.to<JsonObject>());
    assert(api["current"]["power_w"] == 500 && api["current"]["energy_wh"] == 12000);
    NetworkSettings.connected = false; assert(!service.requestRefresh(Domain::Forecast)); testNow += 60000; scheduler.execute(); runWorkers();
    service.snapshot(Domain::Forecast, data, status); assert(!strcmp(status.error, "network_unavailable"));
    NetworkSettings.connected = true; testNow += 15000; assert(service.requestRefresh(Domain::Forecast)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Forecast, data, status); assert(!status.error && status.fresh);
    // Actual native adapter path, with only the network boundary replaced.
    settings = {}; settings.enabled = true; settings.source = Source::EnergyCharts;
    settings.pricing = { true, 1900, 123400, 1234, 9990000 };
    assert(service.configure(Domain::Tariff, settings));
    Service priceReboot; Scheduler priceScheduler; priceReboot.init(priceScheduler);
    api.clear(); priceReboot.serialize(Domain::Tariff, api.to<JsonObject>());
    assert(api["config"]["pricing"]["vat_basis_points"] == 1900);
    assert(api["config"]["pricing"]["grid_micros_per_kwh"] == 123400);
    assert(api["config"]["pricing"]["markup_micros_per_kwh"] == 1234);
    assert(api["config"]["pricing"]["monthly_micros"] == 9990000);
    doc.clear(); doc["unit"] = "EUR / MWh";
    for (unsigned i = 0; i < 8; ++i) { doc["unix_seconds"].add(uint32_t(time(nullptr)) + i * 900); doc["price"].add(i ? 200.0 : -50.0); }
    httpBody.clear(); serializeJson(doc, httpBody); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.planningUsable && data.intervals[0].value == -50000);
    api.clear(); service.serialize(Domain::Tariff, api.to<JsonObject>());
    assert(api["source_fresh"].isNull() && api["source_age_seconds"].isNull());
    assert(api["transport_fresh"] == true && api["planning_usable"] == true);
    assert(!service.requestRefresh(Domain::Tariff)); // Short request spacing still applies; the ordinary interval does not block manual refresh.
    testNow += 3600000; httpAvailable = false; scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.planningUsable && !strcmp(status.error, "provider_connect_failed"));
    api.clear(); service.serialize(Domain::Tariff, api.to<JsonObject>());
    assert(api["planning_usable"] == true && api["last_error"] == "provider_connect_failed");
    assert(api["transport_age_seconds"].as<unsigned>() >= 3600);
    assert(!service.requestRefresh(Domain::Tariff)); // Retry floor also applies to Refresh now.
    testNow += 3600000; httpAvailable = true; scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.planningUsable && !status.error);
    settings = {}; settings.enabled = true; settings.source = Source::ForecastSolar; settings.planeCount = 2;
    for (size_t i = 0; i < 2; ++i) { settings.planes[i].enabled = true; strcpy(settings.planes[i].name, i ? "East" : "West"); settings.planes[i].peakW = 1000; }
    assert(service.configure(Domain::Forecast, settings));
    doc.clear(); doc["message"]["code"] = 0; doc["message"]["type"] = "success";
    time_t now = time(nullptr); struct tm utc {}; gmtime_r(&now, &utc); char key[32]; strftime(key, sizeof(key), "%Y-%m-%dT%H:%M:%S+00:00", &utc);
    doc["message"]["info"]["time_utc"] = key;
    doc["message"]["ratelimit"]["limit"] = 12; doc["message"]["ratelimit"]["period"] = 3600;
    for (unsigned i = 0; i < 5; ++i) { time_t t = now + i * 3600; gmtime_r(&t, &utc); strftime(key, sizeof(key), "%Y-%m-%d %H:%M:%S", &utc); doc["result"]["watt_hours_period"][key] = i ? 1000 : 0; }
    httpBody.clear(); serializeJson(doc, httpBody);
    // One, two and six configured planes all use the same serial worker.
    for (unsigned count : {1U, 2U, 6U}) {
        settings.planeCount = count;
        for (auto& plane : settings.planes) { plane.enabled = true; strcpy(plane.name, "PV"); plane.peakW = 1000; }
        assert(service.configure(Domain::Forecast, settings)); solarRequests = 0; delays.clear(); testNow += 15000;
        scheduler.execute(); runWorkers(); service.snapshot(Domain::Forecast, data, status);
        assert(data.count == 4 && data.intervals[0].value == int(count * 1000) && status.planningUsable);
        assert(solarRequests == count && tasks.size() == 1);
        assert(delays.size() == count - 1); for (auto delay : delays) { assert(delay >= 5000); }
    }
    settings.planeCount = 2; strcpy(settings.planes[0].name, "West"); strcpy(settings.planes[1].name, "East"); assert(service.configure(Domain::Forecast, settings));
    solarRequests = 0; testNow += 15000; scheduler.execute(); runWorkers();
    service.snapshot(Domain::Forecast, data, status); assert(data.intervals[0].value == 2000);
    auto accepted = data; auto fetchedAt = status.fetchedAt;
    testNow += 7200000; solarRequests = 0; failSolarRequest = 2;
    scheduler.execute(); runWorkers(); service.snapshot(Domain::Forecast, data, status);
    assert(solarRequests == 2 && status.hasData && !strcmp(status.error, "provider_connect_failed"));
    assert(data.count == accepted.count && data.generatedAt == accepted.generatedAt && status.fetchedAt == fetchedAt);
    for (unsigned i = 0; i < data.count; ++i) { assert(data.intervals[i].value == accepted.intervals[i].value); }
    api.clear(); service.serialize(Domain::Forecast, api.to<JsonObject>());
    assert(api["plane_status"][0]["http_status"] == 200 && api["plane_status"][1]["http_status"] == -1);
    assert(api["plane_status"][1]["name"] == "East");
    assert(api["plane_status"][1]["state"] == "failed" && api["plane_status"][1]["attempted_at"].as<uint32_t>() > 0);
    assert(api["plane_status"][0]["generated_at"] == now && api["plane_status"][0]["provider_age_seconds"] == 0);
    assert(api["plane_status"][0]["transport_age_seconds"] == 0);
    assert(api["plane_status"][0]["rate_limit"] == 12);
    assert(api["plane_status"][0]["request"]["rate_period_seconds"] == 3600);
    failSolarRequest = 0;
    // A failure on East before West is attempted cannot present West's prior
    // successful transport as a result of the current refresh.
    strcpy(settings.planes[0].name, "East"); strcpy(settings.planes[1].name, "West");
    assert(service.configure(Domain::Forecast, settings)); solarRequests = 0; testNow += 15000;
    assert(service.requestRefresh(Domain::Forecast));
    scheduler.execute(); runWorkers();
    service.snapshot(Domain::Forecast, data, status); accepted = data; fetchedAt = status.fetchedAt;
    api.clear(); service.serialize(Domain::Forecast, api.to<JsonObject>());
    auto westFetched = api["plane_status"][1]["fetched_at"].as<uint32_t>();
    testNow += 7200000; solarRequests = 0; failSolarRequest = 1;
    scheduler.execute(); runWorkers();
    api.clear(); service.serialize(Domain::Forecast, api.to<JsonObject>());
    assert(solarRequests == 1 && api["plane_status"][0]["name"] == "East" && api["plane_status"][0]["state"] == "failed");
    assert(api["plane_status"][1]["name"] == "West" && api["plane_status"][1]["state"] == "not_attempted");
    assert(api["plane_status"][1]["attempted_at"] == 0 && api["plane_status"][1]["http_status"] == 0);
    assert(api["plane_status"][1]["fetched_at"] == westFetched && api["plane_status"][1]["generated_at"] == now);
    assert(api["plane_status"][1]["rate_limit"] == -1 && api["plane_status"][1]["last_error"].isNull());
    assert(!service.requestRefresh(Domain::Forecast)); // A failed cycle cannot spin immediately.
    failSolarRequest = 0;
    // A server hold on HTTP 200 also stops the remaining planes and retains the
    // complete previously accepted site. It cannot be bypassed manually.
    testNow += 7200000; solarRequests = 0; nativeRetrySeconds = 900;
    scheduler.execute(); runWorkers(); service.snapshot(Domain::Forecast, data, status);
    assert(solarRequests == 1 && status.hasData && !strcmp(status.error, "provider_backoff"));
    assert(data.count == accepted.count && status.fetchedAt == fetchedAt);
    for (unsigned i = 0; i < data.count; ++i) { assert(data.intervals[i].value == accepted.intervals[i].value); }
    auto held = service.requestRefresh(Domain::Forecast);
    assert(!held && !strcmp(held.reason, "provider_backoff") && held.retryAfterSeconds == 900);
    api.clear(); service.serialize(Domain::Forecast, api.to<JsonObject>());
    assert(api["plane_status"][0]["request"]["retry_after_seconds"] == 900);
    assert(api["plane_status"][1]["state"] == "not_attempted");
    assert(api["plane_status"][1]["last_error"].isNull() && api["plane_status"][1]["http_status"] == 0);
    nativeRetrySeconds = 0;
    testNow += 7200000; solarRequests = 0; solarRemaining = 0;
    testNow += 15000; scheduler.execute(); runWorkers(); service.snapshot(Domain::Forecast, data, status);
    assert(solarRequests == 1 && status.hasData && !strcmp(status.error, "provider_rate_limited"));
    assert(data.intervals[0].value == 2000); solarRemaining = -1;
    // A config generation change in the second request cannot publish the old run.
    testNow += 7200000; solarRequests = 0;
    duringSolar = [&](unsigned n) { if (n == 2) { auto replacement = settings; replacement.enabled = false; assert(service.configure(Domain::Forecast, replacement)); } };
    scheduler.execute(); runWorkers(); service.snapshot(Domain::Forecast, data, status);
    assert(solarRequests == 2 && !status.enabled && !status.hasData);
    duringSolar = {};
    // A generation change between requests stops the obsolete run before another HTTPS request.
    assert(service.configure(Domain::Forecast, settings)); solarRequests = 0;
    duringDelay = [&] { auto replacement = settings; replacement.planes[1].peakW = 2000; assert(service.configure(Domain::Forecast, replacement)); };
    testNow += 15000; scheduler.execute(); runWorkers(); service.snapshot(Domain::Forecast, data, status);
    assert(solarRequests == 1 && status.enabled && !status.hasData);
    for (auto task : tasks) { delete task; }
    std::cout << "Energy settings persistence/reload, push, HTTP failure/recovery, shared scheduling and generation tests passed\n";
}
