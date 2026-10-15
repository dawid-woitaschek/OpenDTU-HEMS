// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/ProfileSession.h>

namespace Hems::Actuation {

CommandResult ProfileSession::runPlan(RegisterIo const& io, Profiles::ControlPlan const& plan,
        CommandStage stage, bool verifyReadback)
{
    auto result = Actuation::execute(io, plan, verifyReadback);
    if (!result) {
        CommandResult command;
        command.error = CommandError::ExecutionFailed;
        command.stage = stage;
        command.execution = result;
        return command;
    }
    CommandResult command;
    command.stage = stage;
    command.execution = result;
    return command;
}

CommandResult ProfileSession::acquire(RegisterIo const& io, Profiles::Profile const& profile,
        bool verifyReadback)
{
    if (_owned && !_acquireUncertain) { return {}; }

    Profiles::ControlPlan plan;
    auto planned = Profiles::planAcquire(profile, plan);
    if (!planned) {
        CommandResult command;
        command.error = CommandError::PlanningFailed;
        command.stage = CommandStage::Acquire;
        command.planning = planned;
        return command;
    }

    auto result = runPlan(io, plan, CommandStage::Acquire, verifyReadback);
    if (result && plan.count) {
        _owned = true;
        _acquireUncertain = false;
    } else if (result.execution.writeAttempted) {
        // The acquire may have reached the device even if its response or
        // verification failed. Retain ownership for best-effort release and retry
        // acquire on the next normal command.
        _owned = true;
        _acquireUncertain = true;
    }
    return result;
}

CommandResult ProfileSession::execute(RegisterIo const& io, Profiles::Profile const& profile,
        char const* operation, bool hasInput, double input, bool verifyReadback)
{
    // Validate the semantic command before taking ownership of the device.
    Profiles::ControlPlan operationPlan;
    auto planned = Profiles::planOperation(profile, operation, hasInput, input, operationPlan);
    if (!planned) {
        CommandResult command;
        command.error = CommandError::PlanningFailed;
        command.stage = CommandStage::Operation;
        command.planning = planned;
        return command;
    }

    // Profiles must provide a bounded semantic release.
    Profiles::ControlPlan releasePlan;
    planned = Profiles::planRelease(profile, releasePlan);
    if (!planned) {
        CommandResult command;
        command.error = CommandError::PlanningFailed;
        command.stage = CommandStage::Release;
        command.planning = planned;
        return command;
    }

    // Validate acquire before issuing any command.
    Profiles::ControlPlan acquirePlan;
    planned = Profiles::planAcquire(profile, acquirePlan);
    if (!planned) {
        CommandResult command;
        command.error = CommandError::PlanningFailed;
        command.stage = CommandStage::Acquire;
        command.planning = planned;
        return command;
    }
    if (io.authorized && !io.authorized(io.context)) {
        CommandResult command;
        command.error = CommandError::ExecutionFailed;
        command.execution.error = ExecutionError::AuthorizationRevoked;
        return command;
    }
    auto acquired = acquire(io, profile, verifyReadback);
    if (!acquired) { return acquired; }
    auto result = runPlan(io, operationPlan, CommandStage::Operation, verifyReadback);
    // Acquire-less profiles establish ownership on the first operation write.
    if (result.execution.writeAttempted || (result && operationPlan.count)) { _owned = true; }
    return result;
}

CommandResult ProfileSession::release(RegisterIo const& io, Profiles::Profile const& profile,
        bool force, bool verifyReadback)
{
    if (!_owned && !force) { return {}; }

    Profiles::ControlPlan plan;
    auto planned = Profiles::planRelease(profile, plan);
    if (!planned) {
        CommandResult command;
        command.error = CommandError::PlanningFailed;
        command.stage = CommandStage::Release;
        command.planning = planned;
        return command;
    }

    auto result = runPlan(io, plan, CommandStage::Release, verifyReadback);
    if (result) {
        _owned = false; _acquireUncertain = false;
    }
    return result;
}

} // namespace Hems::Actuation
