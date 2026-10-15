// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/Mode.h>
#include <hems/constraints/Constraints.h>
#include <hems/model/State.h>

namespace Hems::Policies {

// EV protection becomes restrictive immediately when fresh telemetry confirms
// charging. This debounce applies only when charging has stopped, so brief EVSE
// pauses cannot make storage discharge into an EV session that resumes moments later.
constexpr uint32_t EvProtectionStopDebounceMs = 120000;
constexpr uint32_t EvProtectionDebounceMs = EvProtectionStopDebounceMs;

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
    Constraints::ConstraintSet actionableConstraints;
};

class EvProtectionPolicy {
public:
    void reset();
    void update(Model::EvseState const& evse, uint32_t nowMs, ControlCapability capability = {}, bool active = false);
    EvProtectionSnapshot snapshot(uint32_t nowMs) const;

private:
    bool _controlEnabled = false;
    bool _chargingKnown = false;
    bool _charging = false;
    bool _conditionActive = false; // valid not-charging stop-debounce window
    uint32_t _conditionSinceMs = 0;
    EvProtectionState _state = EvProtectionState::Inactive;
};

char const* evProtectionStateName(EvProtectionState state);

} // namespace Hems::Policies
