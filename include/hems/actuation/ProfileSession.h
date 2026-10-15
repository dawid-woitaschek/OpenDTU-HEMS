// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/actuation/Executor.h>
#include <hems/profiles/ControlPlan.h>

namespace Hems::Actuation {

enum class CommandStage : uint8_t {
    None,
    Acquire,
    Operation,
    Release,
};

enum class CommandError : uint8_t {
    None,
    PlanningFailed,
    ExecutionFailed,
    OwnershipOrphaned,
};

struct CommandResult {
    CommandError error = CommandError::None;
    CommandStage stage = CommandStage::None;
    Profiles::ControlPlanResult planning {};
    ExecutionResult execution {};
    explicit operator bool() const { return error == CommandError::None; }
};

class ProfileSession {
public:
    CommandResult execute(RegisterIo const& io, Profiles::Profile const& profile,
            char const* operation, bool hasInput = false, double input = 0,
            bool verifyReadback = true);
    CommandResult release(RegisterIo const& io, Profiles::Profile const& profile,
            bool force = false, bool verifyReadback = true);
    void reset() { _owned = false; }
    bool ownsControl() const { return _owned; }

private:
    CommandResult runPlan(RegisterIo const& io, Profiles::ControlPlan const& plan,
            CommandStage stage, bool verifyReadback);
    CommandResult acquire(RegisterIo const& io, Profiles::Profile const& profile,
            bool verifyReadback);

    bool _owned = false;
};

} // namespace Hems::Actuation
