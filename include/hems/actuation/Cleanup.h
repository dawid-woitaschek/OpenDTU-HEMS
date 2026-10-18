// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/actuation/ProfileSession.h>

namespace Hems::Actuation {

enum class CleanupState : uint8_t {
    Idle,
    Pending,
    Running,
    Failed,
    Succeeded,
};

struct CleanupSnapshot {
    CleanupState state = CleanupState::Idle;
    bool retainedContext = false;
    uint32_t attempts = 0;
    CommandResult lastResult {};
};

inline constexpr char const* cleanupStateName(CleanupState state)
{
    switch (state) {
        case CleanupState::Idle: return "idle";
        case CleanupState::Pending: return "pending";
        case CleanupState::Running: return "running";
        case CleanupState::Failed: return "failed";
        case CleanupState::Succeeded: return "succeeded";
    }
    return "unknown";
}

inline constexpr char const* commandStageName(CommandStage stage)
{
    switch (stage) {
        case CommandStage::None: return "none";
        case CommandStage::Acquire: return "acquire";
        case CommandStage::Operation: return "operation";
        case CommandStage::Release: return "release";
    }
    return "unknown";
}

inline constexpr char const* commandErrorName(CommandError error)
{
    switch (error) {
        case CommandError::None: return "none";
        case CommandError::PlanningFailed: return "planning_failed";
        case CommandError::ExecutionFailed: return "execution_failed";
    }
    return "unknown";
}

} // namespace Hems::Actuation
