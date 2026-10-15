// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <hems/evse/SettingsStore.h>
#include <hems/model/Normalize.h>
#include <hems/storage/SettingsStore.h>
#include <hems/model/Sources.h>
#include <Arduino.h>
#include <Configuration.h>

Hems::Controller HemsController;

namespace Hems {

bool Controller::providerAuthorization(void* context, Actuation::Command const& command)
{
    if (!context) { return false; }
    auto& controller = *static_cast<Controller*>(context);
    return controller.readiness(command.target, command.automatic, false).allowed() && controller.constraintsPermit(command);
}

void Controller::init(Scheduler& scheduler)
{
    Evse::BindingSettings.init();
    Storage::StorageSettings.init();
    _warp.setAuthorization(this, providerAuthorization);
    _storage.setAuthorization(this, providerAuthorization);

    scheduler.addTask(_loopTask);
    _loopTask.setCallback([this] {
        _warp.loop();
        _storage.loop();

        auto normalized = Model::normalize(_warp.snapshot(), _storage.snapshot());
        {
            std::lock_guard<std::mutex> lock(_policyMutex);
            _evProtectionPolicy.update(normalized.evse, millis(), controlCapability(), getMode() == Mode::Active);
        }
        applyEvProtection();
    });
    _loopTask.setInterval(100);
    _loopTask.setIterations(TASK_FOREVER);
    _loopTask.enable();
    updateSettings();
}

void Controller::updateSettings()
{
    auto previous = getMode();
    auto const& config = Configuration.get().Hems;
    _mode.store(config.Version == CONFIG_VERSION_HEMS && config.Enabled
            ? config.Mode : Mode::Disabled);
    bool monitor = getMode() != Mode::Disabled;
    if (previous == Mode::Active && getMode() != Mode::Active) {
        _warp.requestRelease(); _storage.requestRelease();
        _evProtectionOwned = false; _policyCommandId = 0;
    }
    _warp.configure(config.Warp, Evse::BindingSettings.get(), monitor);
    _storage.configure(Storage::StorageSettings.get(), monitor);
    if (!monitor) {
        std::lock_guard<std::mutex> lock(_policyMutex);
        _evProtectionPolicy.reset();
    }
}

Mode Controller::getMode() const
{
    return _mode.load();
}

void Controller::getControlProfileId(ControlTarget target, char* buffer, size_t capacity) const
{
    if (target == ControlTarget::Evse) { _warp.controlProfileId(buffer, capacity); }
    else if (target == ControlTarget::Storage) { _storage.controlProfileId(buffer, capacity); }
    else if (capacity) { buffer[0] = 0; }
}

ControlMode Controller::getControlMode(ControlTarget target) const
{
    switch (target) {
        case ControlTarget::Evse: return Evse::BindingSettings.get().Control;
        case ControlTarget::Storage: return Storage::StorageSettings.get().Control;
        default: return ControlMode::MonitorOnly;
    }
}

bool Controller::controlPermissionGranted(ControlTarget target) const
{
    return getControlMode(target) == ControlMode::HemsControl;
}

Actuation::Readiness Controller::getActuationReadiness(ControlTarget target) const
{
    return readiness(target, false);
}
Actuation::Readiness Controller::readiness(ControlTarget target, bool automatic, bool includeBusy) const
{
    Actuation::ReadinessInput input;
    input.globalActive = !automatic || getMode() == Mode::Active;
    input.permissionGranted = controlPermissionGranted(target);
    if (includeBusy) {
        auto state = getActuationCommandSnapshot(target).state;
        input.commandBusy = state == Actuation::MailboxState::Pending || state == Actuation::MailboxState::Running
            || (target == ControlTarget::Storage ? _storage.workerBusy() : _warp.workerBusy());
    }

    switch (target) {
        case ControlTarget::Evse: {
            auto snapshot = _warp.snapshot();
            input.configured = snapshot.configured;
            input.enabled = snapshot.enabled;
            input.profileControllable = _warp.controlCapable();
            input.reachable = snapshot.reachable;
            input.telemetryFresh = snapshot.valid && !snapshot.stale;
            break;
        }
        case ControlTarget::Storage: {
            auto snapshot = _storage.snapshot();
            input.configured = snapshot.configured;
            input.enabled = snapshot.enabled;
            input.profileControllable = _storage.controlCapable();
            input.reachable = snapshot.reachable;
            input.telemetryFresh = snapshot.valid && !snapshot.stale;
            break;
        }
        default:
            break;
    }

    return Actuation::evaluateReadiness(input);
}

bool Controller::actuationAllowed(ControlTarget target) const
{
    return getActuationReadiness(target).allowed();
}

ControlCapability Controller::controlCapability() const
{
    return { controlPermissionGranted(ControlTarget::Storage), controlPermissionGranted(ControlTarget::Evse) };
}

Actuation::ProfileInfo Controller::getProfileInfo(ControlTarget target) const
{
    return target == ControlTarget::Storage ? _storage.profileInfo() : _warp.profileInfo();
}

bool Controller::submitManual(Actuation::Command command, uint32_t& id)
{
    command.automatic = false;
    return submit(command, id);
}

bool Controller::submitActuation(Actuation::Command command, uint32_t& id)
{
    command.automatic = true;
    return submit(command, id);
}

bool Controller::submit(Actuation::Command command, uint32_t& id)
{
    id = 0;
    if (!Actuation::validCommand(command) || !readiness(command.target, command.automatic).allowed()) { return false; }
    if (!constraintsPermit(command)) { return false; }
    return command.target == ControlTarget::Evse ? _warp.submitCommand(command, id)
        : command.target == ControlTarget::Storage && _storage.submitCommand(command, id);
}

bool Controller::constraintsPermit(Actuation::Command const& command) const
{
    // Rechecked by the worker immediately before every bounded write too.
    return command.target != ControlTarget::Storage || command.action != Actuation::Action::SetDischargePowerW
        || command.value <= 0 || getConstraints().storage.dischargeAllowed;
}

void Controller::applyEvProtection()
{
    // No automatic producer in Monitor/Disabled, and EVSE write permission is irrelevant.
    if (getMode() != Mode::Active || !controlCapability().storage) {
        _evProtectionOwned = false; _policyCommandId = 0; _lastPolicyAttemptMs = 0;
        return;
    }
    auto generation = _storage.generation();
    if (_evProtectionGeneration != generation) {
        _evProtectionGeneration = generation;
        _evProtectionOwned = false; _policyCommandId = 0; _lastPolicyAttemptMs = 0;
    }
    auto current = _storage.commandSnapshot();
    if (_policyCommandId && current.id == _policyCommandId) {
        if (current.state == Actuation::MailboxState::Pending || current.state == Actuation::MailboxState::Running) { return; }
        if (current.command.action == Actuation::Action::Stop
                && (current.state == Actuation::MailboxState::Succeeded || current.state == Actuation::MailboxState::Failed)) {
            // Queueing alone is not an intervention: cancelled/revoked work may never write.
            _evProtectionOwned = _evProtectionOwned || _storage.commandResult().execution.writeAttempted;
        }
        _policyCommandId = 0;
    }
    if (!current.command.automatic && current.state == Actuation::MailboxState::Succeeded
            && current.command.action != Actuation::Action::SetTargetSocPct) {
        // A later manual operating command supersedes the protection stop.
        _evProtectionOwned = false;
    }
    bool block = !getConstraints().storage.dischargeAllowed;
    auto storage = _storage.snapshot();
    bool observedDischarge = storage.valid && !storage.stale
        && storage.sample.acPowerW.valid && storage.sample.acPowerW.value < 0;
    if (storage.valid && !storage.stale && storage.sample.acPowerW.valid && storage.sample.acPowerW.value > 0) {
        // Charging has superseded the stopped state; do not release that charging mode later.
        _evProtectionOwned = false;
    }
    if (block && !observedDischarge) { return; }
    if (!block && !_evProtectionOwned) { return; }
    auto now = millis();
    if (_lastPolicyAttemptMs && now - _lastPolicyAttemptMs < 5000) { return; }
    Actuation::Command command; command.target = ControlTarget::Storage;
    command.action = block ? Actuation::Action::Stop : Actuation::Action::Release;
    uint32_t id;
    if (submitActuation(command, id)) {
        _lastPolicyAttemptMs = now;
        _policyCommandId = id;
        if (!block) { _evProtectionOwned = false; } // Best-effort release, once.
    }
}

bool Controller::cancelPendingActuation(ControlTarget target, uint32_t id)
{
    switch (target) {
        case ControlTarget::Evse: return _warp.cancelPendingCommand(id);
        case ControlTarget::Storage: return _storage.cancelPendingCommand(id);
        default: return false;
    }
}

Actuation::MailboxSnapshot Controller::getActuationCommandSnapshot(ControlTarget target) const
{
    switch (target) {
        case ControlTarget::Evse: return _warp.commandSnapshot();
        case ControlTarget::Storage: return _storage.commandSnapshot();
        default: return {};
    }
}

Actuation::CommandResult Controller::getActuationCommandResult(ControlTarget target) const
{
    return target == ControlTarget::Evse ? _warp.commandResult() : _storage.commandResult();
}
bool Controller::ownershipUncertain(ControlTarget target) const
{
    return target == ControlTarget::Evse ? _warp.ownershipUncertain() : _storage.ownershipUncertain();
}

Actuation::CleanupSnapshot Controller::getActuationCleanupSnapshot(ControlTarget target) const
{
    switch (target) {
        case ControlTarget::Evse: return _warp.cleanupSnapshot();
        case ControlTarget::Storage: return _storage.cleanupSnapshot();
        default: return {};
    }
}

Warp::Snapshot Controller::getWarpSnapshot() const
{
    return _warp.snapshot();
}

Storage::Snapshot Controller::getStorageSnapshot() const
{
    return _storage.snapshot();
}

Model::SystemState Controller::getNormalizedState() const
{
    auto result = Model::normalize(_warp.snapshot(), _storage.snapshot());
    Model::normalizeSources(result);
    return result;
}

Constraints::ConstraintSet Controller::getConstraints() const
{
    if (getMode() != Mode::Active || !controlCapability().storage) { return {}; }
    std::lock_guard<std::mutex> lock(_policyMutex);
    return _evProtectionPolicy.snapshot(millis()).actionableConstraints;
}

Policies::EvProtectionSnapshot Controller::getEvProtectionShadowSnapshot() const
{
    std::lock_guard<std::mutex> lock(_policyMutex);
    return _evProtectionPolicy.snapshot(millis());
}

} // namespace Hems
