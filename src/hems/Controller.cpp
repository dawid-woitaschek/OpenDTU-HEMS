// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <hems/evse/SettingsStore.h>
#include <hems/model/Normalize.h>
#include <hems/storage/SettingsStore.h>
#include <hems/model/Sources.h>
#include <Arduino.h>
#include <Configuration.h>
#include <hems/optimizer/Service.h>
#include <ctime>

Hems::Controller HemsController;

namespace Hems {

bool Controller::providerAuthorization(void* context, Actuation::Command const& command)
{
    if (!context) { return false; }
    auto& controller = *static_cast<Controller*>(context);
    return controller.readiness(command.target, command.automatic, false).allowed() && controller.constraintsPermit(command)
        && (command.origin != Actuation::Origin::Economic || controller.economicAuthorized(command));
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
        if (millis() - _economicLastTickMs >= 1000) {
            _economicLastTickMs = millis(); applyEconomic();
        }
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
    command.origin = Actuation::Origin::Manual; command.authority = 0; id = 0;
    if (command.target == ControlTarget::Storage) {
        if (!Actuation::validCommand(command) || !readiness(command.target, false).allowed()
                || !constraintsPermit(command) || !_storage.supportsCommand(command)) { return false; }
        if (!Optimizer::ShadowOptimizer.pauseForManual()) { return false; }
    }
    return submit(command, id);
}

bool Controller::submitActuation(Actuation::Command command, uint32_t& id)
{
    command.automatic = true;
    if (command.origin == Actuation::Origin::Manual) { command.origin = Actuation::Origin::Protection; }
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
    if (command.target != ControlTarget::Storage || command.value <= 0) { return true; }
    auto constraints = getConstraints().storage;
    if (command.action == Actuation::Action::SetDischargePowerW) {
        return constraints.dischargeAllowed && (!constraints.maxDischargePower.active || command.value <= constraints.maxDischargePower.valueW);
    }
    if (command.action == Actuation::Action::SetChargePowerW) {
        return constraints.chargeAllowed && (!constraints.maxChargePower.active || command.value <= constraints.maxChargePower.valueW);
    }
    return true;
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
    if (_storage.owner() != Actuation::Origin::Protection) { _evProtectionOwned = false; }
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

uint64_t Controller::economicContextRevision() const
{
    return (static_cast<uint64_t>(_storage.generation()) << 8) | static_cast<uint8_t>(getMode());
}
bool Controller::economicAuthorized(Actuation::Command const& command) const
{
    auto plan = Optimizer::ShadowOptimizer.dispatchPlan();
    if (!command.automatic || command.target != ControlTarget::Storage || !plan.config.dispatchEnabled
            || plan.paused || !plan.valid || command.authority != plan.revision) { return false; }
    auto owner = _storage.owner();
    if ((_evProtectionOwned || owner == Actuation::Origin::Protection) && plan.batteryW <= 0) { return false; }
    Economic::Context input; input.plan = plan; input.storage = getNormalizedState().storage;
    input.constraints = getConstraints().storage;
    if (plan.batteryW < 0 && !input.constraints.dischargeAllowed) { return false; }
    auto profile = _storage.profileInfo(); input.profile = &profile;
    Actuation::Command desired;
    return Economic::mapCommand(input, desired) && desired.action == command.action
        && desired.hasValue == command.hasValue && (!desired.hasValue || desired.value == command.value);
}
void Controller::applyEconomic()
{
    Economic::Context input;
    input.plan = Optimizer::ShadowOptimizer.dispatchPlan();
    input.mode = getMode(); input.permission = controlPermissionGranted(ControlTarget::Storage);
    input.storage = getNormalizedState().storage; input.constraints = getConstraints().storage;
    auto telemetry = _storage.snapshot();
    input.successfulPolls = telemetry.successfulPolls;
    input.cleanupReady = telemetry.reachable && telemetry.valid && !telemetry.stale;
    input.ready = readiness(ControlTarget::Storage, true).allowed();
    auto profile = _storage.profileInfo(); input.profile = &profile;
    input.providerGeneration = _storage.generation(); input.owner = _storage.owner();
    input.protectionOwned = _evProtectionOwned;
    input.mailbox = _storage.commandSnapshot();
    input.now = time(nullptr); input.nowMs = millis();
    std::lock_guard<std::mutex> lock(_economicMutex);
    auto decision = _economic.update(input);
    if (decision.replan) { Optimizer::ShadowOptimizer.invalidate("economic_command_failed"); }
    if (decision.cancel) {
        Optimizer::ShadowOptimizer.invalidate("economic_work_revoked");
        _storage.cancelPendingCommand(input.mailbox.id);
    }
    if (decision.cleanup) { _storage.releaseEconomic(); }
    if (decision.submit) {
        uint32_t id;
        if (submitActuation(decision.command, id)) { _economic.accepted(decision.command, id, input.nowMs); }
    }
}
void Controller::serializeEconomic(JsonObject output) const
{
    auto plan = Optimizer::ShadowOptimizer.dispatchPlan();
    std::lock_guard<std::mutex> lock(_economicMutex);
    auto state = _economic.snapshot();
    output["enabled"] = plan.config.dispatchEnabled; output["paused_manual"] = plan.paused;
    output["global_mode"] = modeName(getMode()); output["storage_permission"] = controlPermissionGranted(ControlTarget::Storage);
    output["state"] = !plan.config.dispatchEnabled ? "disabled" : plan.paused ? "paused_manual" : state.state;
    output["owner"] = Actuation::originName(_storage.owner());
    output["plan_revision"] = plan.revision; output["energy_revision"] = plan.energyRevision;
    output["plan_valid"] = plan.valid; output["plan_reason"] = plan.reason;
    output["slot_start"] = plan.start; output["slot_end"] = plan.end;
    output["planned_battery_w"] = plan.batteryW;
    output["desired_action"] = Actuation::actionName(state.desired.action);
    output["desired_power_w"] = state.desired.hasValue ? state.desired.value : 0;
    output["last_action"] = Actuation::actionName(state.last.action);
    output["last_power_w"] = state.last.hasValue ? state.last.value : 0;
    output["command_id"] = state.commandId; output["confirmed"] = state.confirmed;
    output["failures"] = state.failures; output["provider_generation"] = _storage.generation();
    auto cleanup = _storage.cleanupSnapshot();
    output["cleanup_state"] = Actuation::cleanupStateName(cleanup.state);
    output["cleanup_pending"] = cleanup.retainedContext;
    output["command_result"] = Actuation::mailboxStateName(_storage.commandSnapshot().state);
    output["command_error"] = Actuation::commandErrorName(_storage.commandResult().error);
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
