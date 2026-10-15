// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <hems/Control.h>
#include <hems/validation/Session.h>
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
    void getControlProfileId(ControlTarget target, char* buffer, size_t capacity) const;
    bool controlPermissionGranted(ControlTarget target) const;
    Actuation::Readiness getActuationReadiness(ControlTarget target) const;
    bool actuationAllowed(ControlTarget target) const;
    Validation::Snapshot getValidationSnapshot() const { return _validation.snapshot(millis()); }
    Actuation::Readiness getValidationReadiness(ControlTarget target) const;
    bool startValidation(uint32_t ttlMs, char* token);
    bool revokeValidation(char const* token);
    bool submitValidation(Actuation::Command const& command, char const* token, uint32_t& id);

    // Internal policy-to-worker bridge. There is intentionally no HTTP endpoint
    // for this path. The current Monitor-only milestone cannot pass the global
    // readiness gate, so submitActuation() remains fail-closed in production.
    bool submitActuation(Actuation::Command const& command, uint32_t& id);
    bool cancelPendingActuation(ControlTarget target, uint32_t id = 0);
    Actuation::MailboxSnapshot getActuationCommandSnapshot(ControlTarget target) const;
    Actuation::CommandResult getActuationCommandResult(ControlTarget target) const;
    bool ownershipUncertain(ControlTarget target) const;
    Actuation::CleanupSnapshot getActuationCleanupSnapshot(ControlTarget target) const;

    Recovery::Snapshot getRecoverySnapshot(ControlTarget target) const;
    bool getRecoveryRecord(ControlTarget target, Recovery::Record& record, Recovery::Snapshot& snapshot) const;
    bool requestRecoveryResolution(ControlTarget target, char const* recordId, bool independentlyReleased);
    Warp::Snapshot getWarpSnapshot() const;
    Storage::Snapshot getStorageSnapshot() const;
    Model::SystemState getNormalizedState() const;
    Constraints::ConstraintSet getConstraints() const;
    Policies::EvProtectionSnapshot getEvProtectionShadowSnapshot() const;

private:
    static bool providerAuthorization(void* context, ControlTarget target);

    Actuation::Readiness readiness(ControlTarget target, bool validation) const;
    void serviceValidation();
    void releaseValidationOwnership();
    Validation::Session _validation;
    std::atomic<Mode> _mode { Mode::Disabled };
    Task _loopTask;
    Warp::Provider _warp;
    Storage::Provider _storage;
    mutable std::mutex _policyMutex;
    Policies::EvProtectionPolicy _evProtectionPolicy;
};

} // namespace Hems

extern Hems::Controller HemsController;
