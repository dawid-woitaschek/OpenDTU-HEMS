// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <hems/Control.h>
#include <hems/Mode.h>
#include <hems/actuation/Cleanup.h>
#include <hems/actuation/Mailbox.h>
#include <hems/actuation/Readiness.h>
#include <hems/constraints/Constraints.h>
#include <hems/economic/Dispatch.h>
#include <hems/flow/Coordinator.h>
#include <hems/model/State.h>
#include <hems/policies/EvProtection.h>
#include <hems/storage/Provider.h>
#include <hems/warp/Provider.h>
#include <mutex>
#include <TaskSchedulerDeclarations.h>

namespace Hems {

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
    ControlCapability controlCapability() const;
    Actuation::ProfileInfo getProfileInfo(ControlTarget target) const;
    bool submitManual(Actuation::Command command, uint32_t& id);
    bool submitActuation(Actuation::Command command, uint32_t& id);
    bool cancelPendingActuation(ControlTarget target, uint32_t id = 0);
    Actuation::MailboxSnapshot getActuationCommandSnapshot(ControlTarget target) const;
    Actuation::CommandResult getActuationCommandResult(ControlTarget target) const;
    bool ownershipUncertain(ControlTarget target) const;
    Actuation::CleanupSnapshot getActuationCleanupSnapshot(ControlTarget target) const;

    Warp::Snapshot getWarpSnapshot() const;
    Storage::Snapshot getStorageSnapshot() const;
    Model::SystemState getNormalizedState() const;
    Model::StorageState getStoragePlanningState() const;
    Model::StoragePowerLimits getStoragePowerLimits() const;
    Constraints::ConstraintSet getConstraints() const;
    Policies::EvProtectionSnapshot getEvProtectionShadowSnapshot() const;
    uint64_t economicContextRevision() const;
    void serializeEconomic(JsonObject output) const;

private:
    static bool providerAuthorization(void* context, Actuation::Command const& command);
    Actuation::Readiness readiness(ControlTarget target, bool automatic, bool includeBusy = true) const;
    bool submit(Actuation::Command command, uint32_t& id);
    bool constraintsPermit(Actuation::Command const& command) const;
    void applyEvProtection();
    void applyEconomic();
    Flow::Decision flowDecision(Optimizer::PlanView const& plan) const;
    bool economicAuthorized(Actuation::Command const& command) const;
    mutable std::mutex _economicMutex;
    Economic::Dispatch _economic;
    Flow::Decision _flow;
    uint32_t _economicLastTickMs = 0, _lastFlowAttemptMs = 0;
    uint32_t _flowStopSlot = 0, _flowStopPoll = 0;
    uint8_t _flowStopAttempts = 0;
    std::atomic<bool> _evProtectionOwned { false };
    uint32_t _evProtectionGeneration = 0;
    std::atomic<uint32_t> _policyCommandId { 0 };
    std::atomic<uint32_t> _lastPolicyAttemptMs { 0 };
    std::atomic<Mode> _mode { Mode::Disabled };
    Task _loopTask;
    Warp::Provider _warp;
    Storage::Provider _storage;
    mutable std::mutex _policyMutex;
    Policies::EvProtectionPolicy _evProtectionPolicy;
};

} // namespace Hems

extern Hems::Controller HemsController;
