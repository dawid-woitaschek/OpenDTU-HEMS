// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Service.h>
#include <Arduino.h>
#include <HttpGetter.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <cassert>
#include <ctime>
#include <iostream>
#include <vector>

extern "C" time_t time(time_t* destination) noexcept
{
    time_t now = 1792180800;
    if (destination) { *destination = now; }
    return now;
}

struct TestTask { void (*entry)(void*); void* context; bool notified = false; };
static std::vector<TestTask*> tasks;
static TestTask* currentTask;
struct Idle {};
int xTaskCreate(void (*entry)(void*), char const*, unsigned, void* context, int, TaskHandle_t* task)
{
    *task = new TestTask { entry, context }; tasks.push_back(*task); return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { task->notified = true; }
uint32_t ulTaskNotifyTake(int, uint32_t)
{
    if (!currentTask->notified) { throw Idle {}; }
    currentTask->notified = false; return 1;
}
void runWorkers()
{
    for (auto task : tasks) {
        if (!task->notified) { continue; }
        currentTask = task;
        try { task->entry(task->context); } catch (Idle const&) {}
    }
}
using namespace Hems::Energy;
std::string payload(Domain domain)
{
    auto now = static_cast<uint32_t>(time(nullptr)); // Deterministic wall-clock seam above.
    JsonDocument doc; doc["schema"] = schema(domain); doc["source"] = "test-http";
    doc["generated_at"] = now; doc["currency"] = "EUR";
    auto interval = doc["intervals"].add<JsonObject>(); interval["start"] = now - 100; interval["end"] = now + 86400;
    interval["start"] = now; // Max per-interval duration is one day.
    interval[domain == Domain::Tariff ? "price_micros_per_kwh" : "power_w"] = 500;
    std::string result; serializeJson(doc, result); return result;
}
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
    httpAvailable = true; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.fresh && !status.error);
    httpBody = "{broken"; assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(status.hasData && status.error);
    httpBody = wire;
    // Late HTTP response must not populate a disabled or replaced configuration generation.
    duringHttp = [&] { assert(service.configure(Domain::Tariff, disabled)); };
    assert(service.requestRefresh(Domain::Tariff)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Tariff, data, status); assert(!status.enabled && !status.hasData);
    // Forecast uses the same worker; a disabled tariff never affects it.
    assert(service.configure(Domain::Forecast, settings)); httpBody = payload(Domain::Forecast);
    scheduler.execute(); runWorkers(); assert(tasks.size() == 1);
    service.snapshot(Domain::Forecast, data, status); assert(status.fresh && data.intervals[0].value == 500);
    service.serialize(Domain::Forecast, api.to<JsonObject>());
    assert(api["current"]["power_w"] == 500 && api["current"]["energy_wh"] == 12000);
    NetworkSettings.connected = false; assert(service.requestRefresh(Domain::Forecast)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Forecast, data, status); assert(!strcmp(status.error, "network_unavailable"));
    NetworkSettings.connected = true; assert(service.requestRefresh(Domain::Forecast)); scheduler.execute(); runWorkers();
    service.snapshot(Domain::Forecast, data, status); assert(!status.error && status.fresh);
    for (auto task : tasks) { delete task; }
    std::cout << "Energy settings persistence/reload, push, HTTP failure/recovery, shared scheduling and generation tests passed\n";
}
