// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/energy/Provider.h>
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
    bool requestRefresh(Domain domain);
    // Consistent normalized input for later optimizer code; caller owns bounded destination.
    void snapshot(Domain domain, Series& series, Status& status) const;
private:
    JsonProvider& provider(Domain domain) { return domain == Domain::Tariff ? _tariff : _forecast; }
    JsonProvider const& provider(Domain domain) const { return domain == Domain::Tariff ? _tariff : _forecast; }
    void loop();
    static void workerEntry(void* context);
    void fetch(Domain domain, Settings const& settings, uint32_t generation);
    mutable std::mutex _mutex;
    JsonProvider _tariff { Domain::Tariff }, _forecast { Domain::Forecast };
    uint32_t _generation[2] {};
    bool _pending[2] {};
    Task _task;
    TaskHandle_t _worker = nullptr;
};
extern Service EnergyData;
} // namespace Hems::Energy
