// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/constraints/Constraints.h>
#include <hems/model/State.h>

namespace Hems::Policies {

constexpr uint32_t EvProtectionDebounceMs = 120000;

enum class EvProtectionState : uint8_t {
    Inactive = 0,
    Waiting,
    WouldBlockStorageDischarge,
};

struct EvProtectionSnapshot {
    EvProtectionState state = EvProtectionState::Inactive;
    bool chargingKnown = false;
    bool charging = false;
    uint32_t conditionAgeMs = 0;
    Constraints::ConstraintSet proposedConstraints;
};

class EvProtectionPolicy {
public:
    void reset();
    void update(Model::EvseState const& evse, uint32_t nowMs);
    EvProtectionSnapshot snapshot(uint32_t nowMs) const;

private:
    bool _chargingKnown = false;
    bool _charging = false;
    bool _conditionActive = false;
    uint32_t _conditionSinceMs = 0;
    EvProtectionState _state = EvProtectionState::Inactive;
};

char const* evProtectionStateName(EvProtectionState state);

} // namespace Hems::Policies
