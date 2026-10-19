// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Hems::Policies {
// User intent for Shadow planning only. Defaults preserve pre-policy optimizer behavior.
struct StorageEconomicPolicy {
    bool gridChargingAllowed = true;
    bool dischargeAllowed = true;
    bool targetEnabled = false;
    uint16_t targetSoc = 70;
    uint32_t targetDeadline = 0; // One-time UTC deadline, never implicitly rolled forward.
};
struct StorageEconomicGoals {
    bool gridChargingAllowed = true, dischargeAllowed = true;
    uint32_t reserveWh = 0, ceilingWh = 0, targetWh = 0, deadline = 0;
    char const* blocker = nullptr;
};
// Existing min/max optimizer fields retain their planning-bound semantics.
StorageEconomicGoals evaluateStorageEconomicPolicy(StorageEconomicPolicy const& policy,
    uint32_t capacityWh, uint16_t minSoc, uint16_t maxSoc, uint32_t now);
} // namespace Hems::Policies
