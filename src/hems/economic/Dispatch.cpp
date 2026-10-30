// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/economic/Dispatch.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace Hems::Economic {
namespace {
bool busy(Actuation::MailboxState state)
{
    return state == Actuation::MailboxState::Pending || state == Actuation::MailboxState::Running;
}
bool same(Actuation::Command const& a, Actuation::Command const& b)
{
    return a.action == b.action && a.hasValue == b.hasValue && (!a.hasValue || a.value == b.value);
}
bool fresh(Model::StorageState const& storage)
{
    return storage.device.enabled && storage.device.reachable && storage.device.hasUpdate
        && storage.socPct.usable() && std::isfinite(storage.socPct.value)
        && !(storage.faultActive.usable() && storage.faultActive.value);
}
} // namespace
bool mapCommand(Context const& in, Actuation::Command& command)
{
    if (!in.profile || !in.profile->control) { return false; }
    Actuation::OperationInfo const* power = nullptr;
    bool stop = false;
    auto action = in.plan.batteryW > 0 ? Actuation::Action::SetChargePowerW : Actuation::Action::SetDischargePowerW;
    for (uint8_t i = 0; i < in.profile->operationCount; ++i) {
        auto const& op = in.profile->operations[i];
        if (op.action == action) { power = &op; }
        if (op.action == Actuation::Action::Stop) { stop = true; }
    }
    return mapPowerCommand(in.plan, in.constraints, in.storage.socPct.value, power, stop, command);
}
bool mapPowerCommand(Optimizer::PlanView const& plan, Constraints::StorageConstraints const& constraints,
    double soc, Actuation::OperationInfo const* operation, bool stopSupported, Actuation::Command& command)
{
    using Actuation::Action;
    command = { ControlTarget::Storage, Action::Stop };
    command.automatic = true; command.origin = Actuation::Origin::Economic;
    command.authority = plan.revision;
    auto power = plan.batteryW;
    if (power > 0) {
        if (!constraints.chargeAllowed || soc >= plan.config.maxSoc) { power = 0; }
    } else if (power < 0) {
        if (!constraints.dischargeAllowed || !plan.config.policies.dischargeAllowed
                || soc <= plan.config.minSoc) { power = 0; }
    }
    command.action = power > 0 ? Action::SetChargePowerW : power < 0 ? Action::SetDischargePowerW : Action::Stop;
    if (!power) { return stopSupported; }
    if (!operation || !operation->hasInput) { return false; }
    if (power) {
        double limit = power > 0 ? plan.config.maxChargeW : plan.config.maxDischargeW;
        auto constraint = power > 0 ? constraints.maxChargePower : constraints.maxDischargePower;
        if (constraint.active) { limit = std::min(limit, std::max(0.0, constraint.valueW)); }
        double value = std::min({ std::abs(static_cast<double>(power)), limit, operation->maximum });
        if (operation->step > 0 && value >= operation->minimum) {
            value = operation->minimum + std::floor((value - operation->minimum) / operation->step + 1e-9) * operation->step;
        }
        // Never round a sub-minimum request up beyond the plan/configured bound.
        if (value < operation->minimum || value <= 0) {
            command.action = Action::Stop;
            return stopSupported;
        }
        command.hasValue = true; command.value = value;
    }
    return true;
}
char const* blocker(Context const& in)
{
    if (!in.plan.config.dispatchEnabled) { return "disabled"; }
    else if (in.plan.paused) { return "paused_manual"; }
    else if (!Flow::economicWearAcknowledged(in.plan.config.flow, in.plan.config.wearSource)) { return "waiting_wear_cost"; }
    else if (in.mode != Mode::Active) { return "waiting_active"; }
    else if (!in.permission) { return "waiting_permission"; }
    else if (!fresh(in.storage)) { return "waiting_telemetry"; }
    else if (!in.plan.valid || in.now < in.plan.start || in.now >= in.plan.end) { return "plan_unavailable"; }
    else if ((in.protectionOwned || in.owner == Actuation::Origin::Protection) && in.plan.batteryW <= 0) { return "blocked_protection"; }
    else if (in.plan.batteryW < 0 && !in.constraints.dischargeAllowed) { return "blocked_protection"; }
    return nullptr;
}
Preview Dispatch::preview(Context const& in) const
{
    Preview out; out.planRevision = in.plan.revision;
    if (_pendingId && in.mailbox.id == _pendingId && in.mailbox.state == Actuation::MailboxState::Failed) {
        // update() will consume this failure and request a replan. Observing it
        // must not predict a fresh command before that normal transition.
        out.reason = "command_failed_replan_pending"; return out;
    }
    if (auto reason = blocker(in)) { out.reason = reason; return out; }
    if (!mapCommand(in, out.command)) { out.reason = "unsupported_action"; return out; }
    if (busy(in.mailbox.state) || !in.ready) { out.reason = "waiting_ready"; return out; }
    if (_state.failures && in.now / 900 == _failureSlot && (in.nowMs - _lastAttemptMs < 30000
            || in.successfulPolls <= _failurePoll || in.plan.revision == _failureRevision || _state.failures >= 3)) {
        out.reason = "retry_wait"; return out;
    }
    out.valid = true; out.reason = "current_interval";
    if (out.command.action == Actuation::Action::Stop && in.plan.batteryW) {
        out.reason = in.plan.batteryW > 0 ? (in.storage.socPct.value >= in.plan.config.maxSoc ? "soc_upper_limit" : "charge_restricted_or_below_minimum")
            : (in.storage.socPct.value <= in.plan.config.minSoc ? "soc_lower_limit" : "discharge_restricted_or_below_minimum");
    }
    return out;
}
Decision Dispatch::update(Context const& in)
{
    using Actuation::Origin;
    using Actuation::MailboxState;
    Decision decision;
    bool generationChanged = _generation != in.providerGeneration;
    _generation = in.providerGeneration;
    if (_pendingId && in.mailbox.id == _pendingId && !busy(in.mailbox.state)) {
        bool success = in.mailbox.state == MailboxState::Succeeded;
        _state.lastState = in.mailbox.state;
        _state.result = (in.mailbox.state == MailboxState::Succeeded || in.mailbox.state == MailboxState::Failed)
            ? in.result : Actuation::CommandResult {};
        _state.confirmed = success && in.owner == Origin::Economic;
        if (in.mailbox.state == MailboxState::Failed) {
            ++_state.failures; _failurePoll = in.successfulPolls;
            _failureRevision = in.plan.revision; _failureSlot = in.now / 900;
            decision.replan = true;
        } else if (success) { _state.failures = 0; }
        _pendingId = 0;
    }
    if (generationChanged) { _state.confirmed = false; }
    if (_pendingId && in.mailbox.id != _pendingId) { _pendingId = 0; _state.confirmed = false; }
    if (in.owner != Origin::Economic) { _state.confirmed = false; }
    if (_state.failures && in.now / 900 != _failureSlot) { _state.failures = 0; }
    _state.planRevision = in.plan.revision; _state.start = in.plan.start; _state.end = in.plan.end;
    _state.cleanupPending = false;
    auto blocked = blocker(in);
    if (blocked) {
        // A usable feed revision often computes the identical action. Retain the
        // confirmed physical state for at most 10 s while replanning, without
        // authorizing any old queued write or extending an expired interval.
        Actuation::Command retained;
        bool grace = _state.confirmed && in.owner == Origin::Economic && !in.protectionOwned && in.plan.inputsUsable
            && in.mode == Mode::Active && in.permission && !in.plan.paused && in.plan.config.dispatchEnabled
            && Flow::economicWearAcknowledged(in.plan.config.flow, in.plan.config.wearSource)
            && fresh(in.storage) && !std::strcmp(in.plan.reason, "energy_revision")
            && in.now >= in.plan.start && in.now < in.plan.end
            && mapCommand(in, retained) && same(_state.last, retained);
        if (grace && !_waitingReplan) { _waitingReplan = true; _invalidSinceMs = in.nowMs; }
        grace = grace && in.nowMs - _invalidSinceMs < 10000;
        if (!grace) { _state.confirmed = false; _waitingReplan = false; }
        _state.state = grace ? "replanning" : blocked;
        _state.cleanupPending = grace;
        // Revocation is safe even before a command establishes ownership.
        decision.cancel = busy(in.mailbox.state) && in.mailbox.command.origin == Origin::Economic && in.mailbox.id != _revokedId;
        if (decision.cancel) { _revokedId = in.mailbox.id; }
        // Manual/protection ownership is never released by economics. Wait for
        // usable telemetry before making a best-effort semantic release.
        decision.cleanup = in.owner == Origin::Economic && in.cleanupReady
            && !busy(in.mailbox.state) && !in.protectionOwned && !grace;
        return decision;
    }
    _waitingReplan = false;
    if (!mapCommand(in, decision.command)) {
        _state.state = "unsupported_action"; _state.confirmed = false;
        decision.cleanup = in.owner == Origin::Economic && in.cleanupReady && !busy(in.mailbox.state) && !in.protectionOwned;
        return decision;
    }
    _state.desired = decision.command;
    if (_state.confirmed && same(_state.last, decision.command)) {
        _state.state = decision.command.action == Actuation::Action::Stop ? "holding"
            : decision.command.action == Actuation::Action::SetChargePowerW ? "charging" : "discharging";
        return decision;
    }
    if (busy(in.mailbox.state) || !in.ready) { _state.state = "waiting_ready"; return decision; }
    if (_state.failures && (in.nowMs - _lastAttemptMs < 30000 || in.successfulPolls <= _failurePoll
            || in.plan.revision == _failureRevision || _state.failures >= 3)) {
        _state.state = "retry_wait"; return decision;
    }
    _state.state = "ready"; decision.submit = true;
    return decision;
}
void Dispatch::accepted(Actuation::Command const& command, uint32_t id, uint32_t nowMs)
{
    _state.last = command; _state.commandId = _pendingId = id;
    _state.lastState = Actuation::MailboxState::Pending;
    _state.result = {};
    _state.confirmed = false; _lastAttemptMs = nowMs; _state.state = "command_pending";
}
} // namespace Hems::Economic
