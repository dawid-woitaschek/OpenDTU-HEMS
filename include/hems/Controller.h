// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <hems/constraints/Constraints.h>
#include <hems/model/State.h>
#include <hems/policies/EvProtection.h>
#include <hems/storage/Provider.h>
#include <hems/warp/Provider.h>
#include <mutex>
#include <TaskSchedulerDeclarations.h>

namespace Hems {

enum class Mode : uint8_t { Disabled, Monitor };

class Controller {
public:
    void init(Scheduler& scheduler);
    void updateSettings();
    Mode getMode() const;
    Warp::Snapshot getWarpSnapshot() const;
    Storage::Snapshot getStorageSnapshot() const;
    Model::SystemState getNormalizedState() const;
    Constraints::ConstraintSet getConstraints() const;
    Policies::EvProtectionSnapshot getEvProtectionShadowSnapshot() const;

private:
    std::atomic<Mode> _mode { Mode::Disabled };
    Task _loopTask;
    Warp::Provider _warp;
    Storage::Provider _storage;
    mutable std::mutex _policyMutex;
    Policies::EvProtectionPolicy _evProtectionPolicy;
};

} // namespace Hems

extern Hems::Controller HemsController;
