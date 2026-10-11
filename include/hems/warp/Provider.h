// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <Configuration.h>
#include <hems/modbus/Client.h>
#include <hems/warp/State.h>
#include <mutex>

namespace Hems::Warp {

// Boot lifetime, like the owning HemsController; safe for late DNS completion.
class Provider {
public:
    void configure(WarpConfig const& config, bool monitor);
    void loop(); // Scheduler only: dispatches, never performs network I/O.
    Snapshot snapshot() const;

private:
    static void workerEntry(void* context);
    void worker();
    mutable std::mutex _mutex;
    WarpConfig _config {};
    bool _enabled = false;
    bool _busy = false;
    bool _polled = false;
    uint32_t _generation = 0;
    uint32_t _lastPollMs = 0;
    State _state;
    Modbus::Client _client;
    TaskHandle_t _worker = nullptr;
};

} // namespace Hems::Warp
