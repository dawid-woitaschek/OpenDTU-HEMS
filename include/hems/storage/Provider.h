// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/modbus/Client.h>
#include <hems/storage/Settings.h>
#include <hems/storage/State.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mutex>

namespace Hems::Storage {

class Provider {
public:
    void configure(StorageConfig const& config, bool monitor);
    void loop(); // Scheduler only: dispatches, never performs network I/O.
    Snapshot snapshot() const;
    bool controlCapable() const;

private:
    static void workerEntry(void* context);
    void worker();

    mutable std::mutex _mutex;
    StorageConfig _config {};
    std::shared_ptr<Profiles::Profile const> _profile;
    bool _enabled = false;
    bool _busy = false;
    bool _polled = false;
    uint32_t _generation = 0;
    uint32_t _lastPollMs = 0;
    uint16_t _port = 0;
    uint8_t _unit = 0;
    State _state;
    Modbus::Client _client;
    TaskHandle_t _worker = nullptr;
};

} // namespace Hems::Storage
