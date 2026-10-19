// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/optimizer/Planner.h>
namespace Hems::Optimizer {
// A copy of only the current interval. Future plan entries never enter a mailbox.
struct PlanView {
    Config config;
    uint32_t revision = 0, energyRevision = 0, start = 0, end = 0;
    int32_t batteryW = 0;
    bool paused = false, valid = false, inputsUsable = false;
    char const* reason = "plan_unavailable";
};
} // namespace Hems::Optimizer
