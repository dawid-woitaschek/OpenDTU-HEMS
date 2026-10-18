// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/energy/Provider.h>
#include <hems/energy/Native.h>
#include <TaskSchedulerDeclarations.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>

namespace Hems::Energy {
// Single optional HTTP worker, independent of policy and Modbus workers. Never writes devices.
class Service {
public:
    void init(Scheduler& scheduler);
    bool configure(Domain domain, Settings const& settings);
    void serialize(Domain domain, JsonObject output, bool all = false) const;
    bool ingest(Domain domain, JsonVariantConst payload);
    RefreshDecision requestRefresh(Domain domain);
    // Consistent normalized input for later optimizer code; caller owns bounded destination.
    void snapshot(Domain domain, Series& series, Status& status) const;
    uint32_t revision() const;
    Status status(Domain domain) const;
    // Share the optional low-priority worker with pure background calculations.
    void background(bool (*due)(void*), void (*run)(void*), void* context);
private:
    JsonProvider& provider(Domain domain) { return domain == Domain::Tariff ? _tariff : _forecast; }
    JsonProvider const& provider(Domain domain) const { return domain == Domain::Tariff ? _tariff : _forecast; }
    RefreshDecision refreshDecision(Domain domain) const; // Caller holds _mutex.
    char const* prerequisite(Domain domain) const;
    void loop();
    static void workerEntry(void* context);
    void runPending(Domain domain);
    void fetch(Domain domain, Settings const& settings, uint32_t generation);
    bool (*_backgroundDue)(void*) = nullptr;
    void (*_backgroundRun)(void*) = nullptr;
    void* _backgroundContext = nullptr;
    struct PlaneStatus { uint32_t fetchedAt = 0; char const* error = nullptr; NativeInfo info; };
    PlaneStatus _planes[MaxPlanes] {};
    NativeInfo _native[2];
    uint8_t _failures[2] {};
    uint32_t _revision = 0, _workerStackFree = 0, _workerFailureMs = 0;
    bool _workerUnavailable = false;
    mutable std::mutex _mutex;
    JsonProvider _tariff { Domain::Tariff }, _forecast { Domain::Forecast };
    uint32_t _generation[2] {};
    bool _pending[2] {}, _running[2] {};
    char const* _stage[2] { "not_attempted", "not_attempted" };
    Task _task;
    TaskHandle_t _worker = nullptr;
};
extern Service EnergyData;
} // namespace Hems::Energy
