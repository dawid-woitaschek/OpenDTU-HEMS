// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/policies/EvProtection.h>

namespace Hems::Policies {

void EvProtectionPolicy::reset()
{
    _chargingKnown = false;
    _charging = false;
    _conditionActive = false;
    _conditionSinceMs = 0;
    _state = EvProtectionState::Inactive;
}

void EvProtectionPolicy::update(Model::EvseState const& evse, uint32_t nowMs)
{
    _chargingKnown = evse.device.enabled && evse.charging.usable();
    _charging = _chargingKnown && evse.charging.value;

    if (!_charging) {
        _conditionActive = false;
        _conditionSinceMs = 0;
        _state = EvProtectionState::Inactive;
        return;
    }

    if (!_conditionActive) {
        _conditionActive = true;
        _conditionSinceMs = nowMs;
        _state = EvProtectionState::Waiting;
        return;
    }

    _state = static_cast<uint32_t>(nowMs - _conditionSinceMs) >= EvProtectionDebounceMs
        ? EvProtectionState::WouldBlockStorageDischarge
        : EvProtectionState::Waiting;
}

EvProtectionSnapshot EvProtectionPolicy::snapshot(uint32_t nowMs) const
{
    EvProtectionSnapshot result;
    result.state = _state;
    result.chargingKnown = _chargingKnown;
    result.charging = _charging;
    if (_conditionActive) {
        result.conditionAgeMs = static_cast<uint32_t>(nowMs - _conditionSinceMs);
    }
    if (_state == EvProtectionState::WouldBlockStorageDischarge) {
        result.proposedConstraints.storage.dischargeAllowed = false;
    }
    return result;
}

char const* evProtectionStateName(EvProtectionState state)
{
    switch (state) {
        case EvProtectionState::Waiting: return "waiting";
        case EvProtectionState::WouldBlockStorageDischarge: return "would_block_storage_discharge";
        case EvProtectionState::Inactive:
        default: return "inactive";
    }
}

} // namespace Hems::Policies
