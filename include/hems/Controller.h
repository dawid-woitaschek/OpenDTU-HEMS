// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <hems/Control.h>
#include <hems/actuation/Cleanup.h>
#include <hems/actuation/Mailbox.h>
#include <hems/actuation/Readiness.h>
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
    ControlMode getControlMode(ControlTarget target) const;
    bool controlPermissionGranted(ControlTarget target) const;
    Actuation::Readiness getActuationReadiness(ControlTarget target) const;
    bool actuationAllowed(ControlTarget target) const;

    // Internal policy-to-worker bridge. There is intentionally no HTTP endpoint
    // for this path. The current Monitor-only milestone cannot pass the global
    // readiness gate, so submitActuation() remains fail-closed in production.
    bool submitActuation(Actuation::Command const& command, uint32_t& id);
    bool cancelPendingActuation(ControlTarget target, uint32_t id = 0);
    Actuation::MailboxSnapshot getActuationCommandSnapshot(ControlTarget target) const;
    Actuation::CleanupSnapshot getActuationCleanupSnapshot(ControlTarget target) const;

    Warp::Snapshot getWarpSnapshot() const;
    Storage::Snapshot getStorageSnapshot() const;
    Model::SystemState getNormalizedState() const;
    Constraints::ConstraintSet getConstraints() const;
    Policies::EvProtectionSnapshot getEvProtectionShadowSnapshot() const;

private:
    static bool providerAuthorization(void* context, ControlTarget target);

    std::atomic<Mode> _mode { Mode::Disabled };
    Task _loopTask;
    Warp::Provider _warp;
    Storage::Provider _storage;
    mutable std::mutex _policyMutex;
    Policies::EvProtectionPolicy _evProtectionPolicy;
};

} // namespace Hems

extern Hems::Controller HemsController;
