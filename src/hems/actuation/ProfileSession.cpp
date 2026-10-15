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
    if (_owned) { return {}; }

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
    } else if (result.execution.writeAttempted) {
        // The acquire may have reached the device even if its response or
        // verification failed. Retain ownership and require release before
        // accepting more commands; do not silently skip a failed acquire.
        _owned = true;
        _acquireUncertain = true;
    }
    return result;
}

CommandResult ProfileSession::execute(RegisterIo const& io, Profiles::Profile const& profile,
        char const* operation, bool hasInput, double input, bool verifyReadback)
{
    if (_acquireUncertain) {
        CommandResult command;
        command.error = CommandError::OwnershipOrphaned;
        command.stage = CommandStage::Acquire;
        return command;
    }
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

    // An empty/missing release cannot resolve persistent ownership. Validate
    // the escape path before any acquire or operation write can occur.
    Profiles::ControlPlan releasePlan;
    planned = Profiles::planRelease(profile, releasePlan);
    if (!planned) {
        CommandResult command;
        command.error = CommandError::PlanningFailed;
        command.stage = CommandStage::Release;
        command.planning = planned;
        return command;
    }

    // A rejected command or invalid acquire must not arm a journal.
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
    if (!_owned && io.prepareOwnership && !io.prepareOwnership(io.context)) {
        CommandResult command;
        command.error = CommandError::PersistenceFailed;
        command.stage = CommandStage::Journal;
        return command;
    }
    auto acquired = acquire(io, profile, verifyReadback);
    if (!acquired) {
        if (!_owned && !acquired.execution.writeAttempted && io.clearOwnership && !io.clearOwnership(io.context)) {
            acquired.error = CommandError::PersistenceFailed;
            acquired.stage = CommandStage::Journal;
        }
        return acquired;
    }
    auto result = runPlan(io, operationPlan, CommandStage::Operation, verifyReadback);
    // Acquire-less profiles establish ownership on the first operation write.
    if (result.execution.writeAttempted || (result && operationPlan.count)) { _owned = true; }
    else if (!_owned && io.clearOwnership && !io.clearOwnership(io.context)) {
        result.error = CommandError::PersistenceFailed;
        result.stage = CommandStage::Journal;
    }
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
        // Release/readback and local ownership publication precede durable clear.
        if (io.clearOwnership && !io.clearOwnership(io.context)) {
            result.error = CommandError::PersistenceFailed;
            result.stage = CommandStage::Journal;
        }
    }
    return result;
}

} // namespace Hems::Actuation
