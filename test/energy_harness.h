// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/energy/Service.h>
#include <Arduino.h>
#include <HttpGetter.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <NtpSettings.h>
#include <cassert>
#include <ctime>
#include <iostream>
#include <vector>

inline uint32_t currentUtc = 1792180800;
extern "C" time_t time(time_t* destination) noexcept
{
    time_t now = currentUtc;
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
static unsigned solarRequests = 0, failSolarRequest = 0;
static int solarRemaining = -1;
static uint32_t nativeRetrySeconds = 0;
static int nativeHttpStatus = 200;
static std::function<void(unsigned)> duringSolar;
namespace Hems::Energy {
bool providerGet(char const*, Source source, JsonDocument& doc, NativeInfo& info, char const*& error) {
    if (source == Source::ForecastSolar) {
        ++solarRequests; info.rateRemaining = solarRemaining;
        if (duringSolar) { duringSolar(solarRequests); }
        if (solarRequests == failSolarRequest) { info.httpStatus = -1; error = "provider_connect_failed"; return false; }
    }
    ++httpRequests; info.dns = 1; info.retrySeconds = nativeRetrySeconds;
    if (duringHttp) { auto callback = std::move(duringHttp); duringHttp = {}; callback(); }
    if (!httpAvailable) { info.httpStatus = -1; error = "provider_connect_failed"; return false; }
    info.httpStatus = nativeHttpStatus;
    if (nativeHttpStatus != 200) { error = "provider_http_error"; return false; }
    bool ok = !deserializeJson(doc, httpBody); error = ok ? nullptr : "malformed_or_oversized_json"; return ok;
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