// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/policies/StorageEconomic.h>

namespace Hems::Policies {
StorageEconomicGoals evaluateStorageEconomicPolicy(StorageEconomicPolicy const& p,
    uint32_t capacityWh, uint16_t minSoc, uint16_t maxSoc, uint32_t now)
{
    StorageEconomicGoals goals;
    goals.gridChargingAllowed = p.gridChargingAllowed;
    goals.dischargeAllowed = p.dischargeAllowed;
    goals.reserveWh = (static_cast<uint64_t>(capacityWh) * minSoc + 99) / 100;
    goals.ceilingWh = static_cast<uint64_t>(capacityWh) * maxSoc / 100;
    if (p.targetEnabled) {
        goals.targetWh = (static_cast<uint64_t>(capacityWh) * p.targetSoc + 99) / 100;
        goals.deadline = p.targetDeadline;
        if (p.targetSoc < minSoc || p.targetSoc > maxSoc || goals.targetWh > goals.ceilingWh) {
            goals.blocker = "target_outside_planning_bounds";
        } else if (p.targetDeadline <= now) { goals.blocker = "target_deadline_passed"; }
    }
    return goals;
}
} // namespace Hems::Policies
