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
        && (command.origin != Actuation::Origin::Economic || controller.economicAuthorized(command))
        && (command.origin != Actuation::Origin::Flow || controller.flowAuthorized(command));
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
        auto policyPlan = Optimizer::ShadowOptimizer.dispatchPlan();
        {
            std::lock_guard<std::mutex> lock(_policyMutex);
            if (policyPlan.config.flow.enabled && !policyPlan.config.flow.evProtection) { normalized.evse.device.enabled = false; _evProtectionPolicy.reset(); }
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
    if (command.target == ControlTarget::Storage) {
        auto plan = Optimizer::ShadowOptimizer.dispatchPlan();
        if (plan.config.flow.enabled) {
            if (command.action == Actuation::Action::Release || command.action == Actuation::Action::SetTargetSocPct) { return false; }
            if (command.action != Actuation::Action::Stop) {
                auto requested = plan;
                requested.batteryW = command.action == Actuation::Action::SetDischargePowerW ? -command.value : command.value;
                auto flow = flowDecision(requested);
                bool safeGridCharge = command.action == Actuation::Action::SetChargePowerW && flow.batteryW > 0
                    && plan.config.policies.gridChargingAllowed;
                if ((!flow.usable && !safeGridCharge) || std::abs(flow.batteryW) + 0.01 < command.value) { return false; }
            }
        }
    }
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
    if (!block && Optimizer::ShadowOptimizer.dispatchPlan().config.flow.enabled) {
        if (_storage.handoffProtectionToFlow()) { _evProtectionOwned = false; }
        return;
    }
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
Flow::Decision Controller::flowDecision(Optimizer::PlanView const& plan) const
{
    return flowDecision(plan, getConstraints().storage);
}
Flow::Decision Controller::flowDecision(Optimizer::PlanView const& plan, Constraints::StorageConstraints const& limits) const
{
    if (!plan.config.flow.enabled) { Flow::Decision d; d.batteryW = plan.batteryW; return d; }
    auto state = getNormalizedState(); auto dpl = Flow::Coordination.dpl();
    uint32_t charge = plan.config.maxChargeW, discharge = plan.config.maxDischargeW;
    auto physical = getStoragePowerLimits();
    if (physical.known) { charge = std::min(charge, physical.chargeW); discharge = std::min(discharge, physical.dischargeW); }
    if (limits.maxChargePower.active) { charge = std::min<double>(charge, std::max(0.0, limits.maxChargePower.valueW)); }
    if (limits.maxDischargePower.active) { discharge = std::min<double>(discharge, std::max(0.0, limits.maxDischargePower.valueW)); }
    auto policy = plan.config.flow;
    if (dpl.atMs && millis() - dpl.atMs <= 3000) { policy.pvAcLimitW = std::min(policy.pvAcLimitW, dpl.physicalAcW); }
    return Flow::coordinate(policy, state, plan.batteryW, charge, discharge,
        limits.chargeAllowed && state.storage.socPct.usable() && state.storage.socPct.value < plan.config.maxSoc,
        limits.dischargeAllowed && plan.config.policies.dischargeAllowed && state.storage.socPct.usable() && state.storage.socPct.value > plan.config.minSoc,
        plan.config.policies.gridChargingAllowed, dpl.available && millis() - dpl.atMs <= 3000, dpl.pvHeadroom,
        dpl.solarDormant && millis() - dpl.atMs <= 3000);
}
bool Controller::flowAuthorized(Actuation::Command const& command) const
{
    auto plan = Optimizer::ShadowOptimizer.dispatchPlan();
    auto owner = _storage.owner();
    return command.automatic && command.target == ControlTarget::Storage && command.action == Actuation::Action::Stop
        && plan.config.flow.enabled && !plan.paused
        && owner != Actuation::Origin::Manual && owner != Actuation::Origin::Protection;
}
bool Controller::economicAuthorized(Actuation::Command const& command) const
{
    auto plan = Optimizer::ShadowOptimizer.dispatchPlan();
    if (!command.automatic || command.target != ControlTarget::Storage || !plan.config.dispatchEnabled
            || plan.paused || !plan.valid || command.authority != plan.revision
            || !Flow::economicWearAcknowledged(plan.config.flow, plan.config.wearSource)) { return false; }
    if (plan.config.flow.enabled) { plan.batteryW = flowDecision(plan).batteryW; }
    auto owner = _storage.owner();
    if ((_evProtectionOwned || owner == Actuation::Origin::Protection) && plan.batteryW <= 0) { return false; }
    auto constraints = getConstraints().storage;
    if (plan.batteryW < 0 && !constraints.dischargeAllowed) { return false; }
    Actuation::OperationInfo power, stop;
    bool hasPower = _storage.operationInfo(plan.batteryW > 0 ? Actuation::Action::SetChargePowerW : Actuation::Action::SetDischargePowerW, power);
    bool hasStop = _storage.operationInfo(Actuation::Action::Stop, stop);
    Actuation::Command desired;
    return Economic::mapPowerCommand(plan, constraints, _storage.snapshot().sample.socPct.value, hasPower ? &power : nullptr, hasStop, desired)
        && desired.action == command.action
        && desired.hasValue == command.hasValue && (!desired.hasValue || desired.value == command.value);
}
namespace {
bool excessiveFlow(Economic::Context const& in, Flow::Decision const& flow, bool authority)
{
    bool discharge = in.storage.acPowerW.usable() && in.storage.acPowerW.value < -std::max(0, -flow.batteryW) - 20;
    bool charge = in.storage.acPowerW.usable() && in.storage.acPowerW.value > std::max(0, flow.batteryW) + 20
        && (!authority || !flow.usable || in.storage.socPct.value >= in.plan.config.maxSoc);
    return discharge || charge;
}
bool needsFlowStop(Economic::Context const& in, Flow::Decision const& flow, bool authority)
{
    return in.plan.config.flow.enabled && !in.plan.paused && in.permission && in.mode == Mode::Active
        && in.owner != Actuation::Origin::Manual && in.owner != Actuation::Origin::Protection && in.cleanupReady
        && in.mailbox.state != Actuation::MailboxState::Pending && in.mailbox.state != Actuation::MailboxState::Running
        && excessiveFlow(in, flow, authority);
}
} // namespace
Economic::Preview Controller::shadowIfActive() const
{
    Economic::Context in;
    in.plan = Optimizer::ShadowOptimizer.dispatchPlan();
    in.mode = Mode::Active; in.permission = true; // Only these two explicit assumptions.
    in.storage = getStoragePlanningState(); in.constraints = getConstraints().storage;
    auto proposed = getEvProtectionShadowSnapshot().proposedConstraints.storage;
    in.constraints.chargeAllowed = in.constraints.chargeAllowed && proposed.chargeAllowed;
    in.constraints.dischargeAllowed = in.constraints.dischargeAllowed && proposed.dischargeAllowed;
    auto telemetry = _storage.snapshot();
    in.successfulPolls = telemetry.successfulPolls; in.cleanupReady = telemetry.reachable && telemetry.valid && !telemetry.stale;
    auto ready = readiness(ControlTarget::Storage, true);
    ready.blockers &= ~(Actuation::readinessBit(Actuation::ReadinessBlocker::GlobalModeInactive)
        | Actuation::readinessBit(Actuation::ReadinessBlocker::DevicePermissionMissing));
    in.ready = ready.allowed();
    auto profile = _storage.profileInfo(); in.profile = &profile;
    in.providerGeneration = _storage.generation(); in.owner = _storage.owner(); in.protectionOwned = _evProtectionOwned;
    in.mailbox = _storage.commandSnapshot(); in.result = _storage.commandResult();
    in.now = time(nullptr); in.nowMs = millis();
    auto requested = in.plan;
    bool authority = requested.valid && requested.config.dispatchEnabled && Flow::economicWearAcknowledged(requested.config.flow, requested.config.wearSource);
    if (!authority) { requested.batteryW = 0; requested.config.maxChargeW = requested.config.maxDischargeW = 0; }
    auto flow = flowDecision(requested, in.constraints);
    std::lock_guard<std::mutex> lock(_economicMutex);
    if (needsFlowStop(in, flow, authority)) {
        Economic::Preview out; out.planRevision = in.plan.revision; out.reason = "flow_stop_wait";
        auto attempts = _flowStopSlot == in.now / 900 ? _flowStopAttempts : 0;
        if (in.ready && attempts < 3 && (!attempts || (in.successfulPolls > _flowStopPoll && in.nowMs - _lastFlowAttemptMs >= 30000))) {
            Actuation::OperationInfo op;
            if (_storage.operationInfo(Actuation::Action::Stop, op)) {
                out.valid = true; out.reason = "flow_protection";
                out.command = {ControlTarget::Storage, Actuation::Action::Stop};
            } else { out.reason = "unsupported_action"; }
        }
        return out;
    }
    if (in.plan.config.flow.enabled) { in.plan.batteryW = flow.batteryW; }
    auto out = _economic.preview(in);
    if (out.valid && in.plan.config.flow.enabled) { out.reason = flow.reason; }
    if (!out.valid && !in.ready) {
        for (auto blocker : {Actuation::ReadinessBlocker::DeviceNotConfigured, Actuation::ReadinessBlocker::DeviceNotEnabled,
                Actuation::ReadinessBlocker::ProfileNotControllable, Actuation::ReadinessBlocker::DeviceUnreachable,
                Actuation::ReadinessBlocker::TelemetryNotFresh, Actuation::ReadinessBlocker::CommandBusy}) {
            if (ready.has(blocker)) { out.reason = Actuation::readinessBlockerName(blocker); break; }
        }
    }
    return out;
}
void Controller::applyEconomic()
{
    Economic::Context input;
    input.plan = Optimizer::ShadowOptimizer.dispatchPlan();
    auto flowPlan = input.plan;
    bool economicAuthority = flowPlan.valid && flowPlan.config.dispatchEnabled
        && Flow::economicWearAcknowledged(flowPlan.config.flow, flowPlan.config.wearSource);
    if (!economicAuthority) {
        flowPlan.batteryW = 0; flowPlan.config.maxChargeW = flowPlan.config.maxDischargeW = 0;
    }
    auto flow = flowDecision(flowPlan);
    bool coordinate = input.plan.config.flow.enabled && getMode() == Mode::Active;
    Flow::Coordination.publish({ static_cast<uint32_t>(millis()), flow.pvLimitW, coordinate });
    if (coordinate) { input.plan.batteryW = flow.batteryW; }
    input.mode = getMode(); input.permission = controlPermissionGranted(ControlTarget::Storage);
    input.storage = getStoragePlanningState(); input.constraints = getConstraints().storage;
    auto telemetry = _storage.snapshot();
    input.successfulPolls = telemetry.successfulPolls;
    input.cleanupReady = telemetry.reachable && telemetry.valid && !telemetry.stale;
    input.ready = readiness(ControlTarget::Storage, true).allowed();
    auto profile = _storage.profileInfo(); input.profile = &profile;
    input.providerGeneration = _storage.generation(); input.owner = _storage.owner();
    input.protectionOwned = _evProtectionOwned;
    input.mailbox = _storage.commandSnapshot(); input.result = _storage.commandResult();
    input.now = time(nullptr); input.nowMs = millis();
    std::lock_guard<std::mutex> lock(_economicMutex);
    _flow = flow;
    if (_flowStopSlot != input.now / 900) { _flowStopSlot = input.now / 900; _flowStopAttempts = 0; }
    if (!excessiveFlow(input, flow, economicAuthority)) { _flowStopAttempts = 0; }
    // Same pure flow-stop guard is used by the read-only Active preview.
    if (coordinate && needsFlowStop(input, flow, economicAuthority)) {
        Actuation::Command stop { ControlTarget::Storage, Actuation::Action::Stop };
        stop.origin = Actuation::Origin::Flow; stop.automatic = true;
        uint32_t id;
        if (_flowStopAttempts < 3 && (!_flowStopAttempts
                || (input.successfulPolls > _flowStopPoll && millis() - _lastFlowAttemptMs >= 30000)) && submitActuation(stop, id)) {
            _lastFlowAttemptMs = millis(); _flowStopPoll = input.successfulPolls; ++_flowStopAttempts;
        }
        return;
    }
    auto decision = _economic.update(input);
    if (decision.replan) { Optimizer::ShadowOptimizer.invalidate("economic_command_failed"); }
    if (decision.cancel) {
        Optimizer::ShadowOptimizer.invalidate("economic_work_revoked");
        _storage.cancelPendingCommand(input.mailbox.id);
    }
    if (!coordinate && !input.protectionOwned) {
        // Existing one-shot retained-endpoint cleanup, restricted to its owner.
        // While coordination still applies, retain the controlled safe Stop on
        // invalid inputs; a fresh valid plan can take it over without Release.
        _storage.releaseFlow();
        if (decision.cleanup) { _storage.releaseEconomic(); }
    }
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
    output["pause_reason"] = plan.paused ? "manual_control" : nullptr;
    output["state"] = !plan.config.dispatchEnabled ? "disabled" : plan.paused ? "paused_manual" : state.state;
    output["owner"] = Actuation::originName(_storage.owner());
    if (_storage.owner() == Actuation::Origin::Flow && !plan.paused
            && (!plan.valid || !_flow.usable || !plan.config.dispatchEnabled)) {
        output["state"] = "holding_flow_protection";
    }
    output["plan_revision"] = plan.revision; output["energy_revision"] = plan.energyRevision;
    output["plan_valid"] = plan.valid; output["plan_reason"] = plan.reason;
    output["slot_start"] = plan.start; output["slot_end"] = plan.end;
    output["coordination_state"] = _flow.reason;
    Actuation::OperationInfo stopOperation;
    bool stopSupported = _storage.operationInfo(Actuation::Action::Stop, stopOperation);
    auto storage = getStoragePlanningState();
    output["export_enforcement_available"] = plan.config.flow.enabled && _flow.exportEnforcement && !plan.paused
        && plan.config.enabled && plan.config.dispatchEnabled && plan.valid && stopSupported
        && Flow::economicWearAcknowledged(plan.config.flow, plan.config.wearSource)
        && readiness(ControlTarget::Storage, true, false).allowed() && _storage.owner() != Actuation::Origin::Manual
        && !(storage.faultActive.usable() && storage.faultActive.value)
        && controlPermissionGranted(ControlTarget::Storage) && getMode() == Mode::Active
        && state.failures == 0 && _flowStopAttempts < 3 && !ownershipUncertain(ControlTarget::Storage);
    output["flow_stop_attempts"] = _flowStopAttempts;
    output["coordinated_battery_w"] = _flow.batteryW;
    output["pv_output_limit_w"] = _flow.pvLimitW;
    output["net_export_limit_w"] = plan.config.flow.maxExportW;
    output["planned_battery_w"] = plan.batteryW;
    output["desired_action"] = Actuation::actionName(state.desired.action);
    output["desired_power_w"] = state.desired.hasValue ? state.desired.value : 0;
    auto commanded = _storage.commandSnapshot();
    output["commanded_action"] = Actuation::actionName(commanded.id ? commanded.command.action : state.last.action);
    output["commanded_power_w"] = commanded.id ? (commanded.command.hasValue ? commanded.command.value : 0) : (state.last.hasValue ? state.last.value : 0);
    output["commanded_origin"] = Actuation::originName(commanded.command.origin);
    output["commanded_id"] = commanded.id;
    output["last_action"] = Actuation::actionName(state.last.action);
    output["last_power_w"] = state.last.hasValue ? state.last.value : 0;
    output["command_id"] = state.commandId; output["confirmed"] = state.confirmed;
    output["last_command_plan_revision"] = state.last.authority;
    output["command_completed_ms"] = state.result.completedMs;
    output["failures"] = state.failures; output["provider_generation"] = _storage.generation();
    auto cleanup = _storage.cleanupSnapshot();
    output["cleanup_state"] = Actuation::cleanupStateName(cleanup.state);
    output["cleanup_pending"] = cleanup.retainedContext || state.cleanupPending;
    output["command_result"] = Actuation::mailboxStateName(state.lastState);
    output["command_error"] = Actuation::commandErrorName(state.result.error);
    output["execution_error"] = Actuation::executionErrorName(state.result.execution.error);
    output["transport_error"] = Modbus::errorName(state.result.execution.transport.error);
    output["readback_verified"] = state.result.execution.readbackVerified;
    output["modbus_exception"] = state.result.execution.transport.exception;
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
Model::StorageState Controller::getStoragePlanningState() const
{
    return Model::normalizeStorage(_storage.snapshot());
}

Model::StoragePowerLimits Controller::getStoragePowerLimits() const
{
    Actuation::OperationInfo operation;
    Model::StoragePowerLimits limits;
    if (!_storage.operationInfo(Actuation::Action::SetChargePowerW, operation) || !operation.hasInput) { return limits; }
    limits.chargeW = std::max(0.0, operation.maximum);
    if (!_storage.operationInfo(Actuation::Action::SetDischargePowerW, operation) || !operation.hasInput) { return {}; }
    limits.dischargeW = std::max(0.0, operation.maximum); limits.known = true;
    return limits;
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
