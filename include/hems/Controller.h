// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <hems/warp/Provider.h>
#include <TaskSchedulerDeclarations.h>

namespace Hems {

enum class Mode : uint8_t { Disabled, Monitor };

class Controller {
public:
    void init(Scheduler& scheduler);
    void updateSettings();
    Mode getMode() const;
    Warp::Snapshot getWarpSnapshot() const;

private:
    std::atomic<Mode> _mode { Mode::Disabled };
    Task _loopTask;
    Warp::Provider _warp;
};

} // namespace Hems

extern Hems::Controller HemsController;
