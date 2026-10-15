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

    if (_chargingKnown && _charging) {
        // Fresh charging evidence is sufficient to block discharge immediately.
        _conditionActive = false;
        _conditionSinceMs = 0;
        _state = EvProtectionState::WouldBlockStorageDischarge;
        return;
    }

    if (!_chargingKnown) {
        // Unknown/stale telemetry is not equivalent to "not charging". If the
        // policy was already protecting an EV session, keep that restriction and
        // require a new continuous fresh-false window before releasing it.
        _conditionActive = false;
        _conditionSinceMs = 0;
        return;
    }

    // At this point charging is known and false.
    if (_state == EvProtectionState::Inactive) {
        _conditionActive = false;
        _conditionSinceMs = 0;
        return;
    }

    if (!_conditionActive) {
        _conditionActive = true;
        _conditionSinceMs = nowMs;
        _state = EvProtectionState::Waiting;
        return;
    }

    if (static_cast<uint32_t>(nowMs - _conditionSinceMs) >= EvProtectionStopDebounceMs) {
        _conditionActive = false;
        _conditionSinceMs = 0;
        _state = EvProtectionState::Inactive;
    }
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
    if (_state == EvProtectionState::WouldBlockStorageDischarge
            || _state == EvProtectionState::Waiting) {
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
