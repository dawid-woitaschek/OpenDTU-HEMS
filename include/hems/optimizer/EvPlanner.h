// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/optimizer/Planner.h>
namespace Hems::Optimizer {
struct EvStep {
    uint32_t start = 0, end = 0, chargeEnd = 0;
    uint16_t currentA = 0;
    int32_t price = 0, residualPvW = 0, storageW = 0;
    int64_t costNano = 0;
};
struct EvPlan {
    EvStep steps[MaxSteps] {};
    uint16_t count = 0;
    uint32_t generatedAt = 0, coverageEnd = 0, completion = 0;
    int64_t plannedWs = 0, deliverableWs = 0, costNano = 0;
    bool pvIncluded = false, storageIncluded = false, storageConflict = false;
    char const* reason = "disabled";
    char const* coverageReason = nullptr;
};
// Staged residual scheduling only. No actuator/controller/producer dependencies.
bool calculateEv(Config const& config, Inputs const& inputs, Plan const& storage, EvPlan& output);
void serializeEv(Config const& config, Inputs const& inputs, EvPlan const& plan,
    JsonObject output, uint32_t now, uint32_t revision, size_t offset = 0, bool stale = false);
} // namespace Hems::Optimizer
