// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/warp/State.h>
#include <cmath>

namespace Hems::Warp {

bool decode(Sample& sample, uint8_t const* evse, uint8_t const* meter)
{
    // Official WARP table: two big-endian registers per value. docs/HEMS_WARP.md.
    sample.iecState = Modbus::decodeUint32(evse);
    sample.chargingState = Modbus::decodeUint32(evse + 4);
    auto currentMA = Modbus::decodeUint32(evse + 20);
    if (sample.iecState > 4 || sample.chargingState > 4 || currentMA > 32000
            || (currentMA != 0 && currentMA < 6000)) { return false; }
    sample.allowedCurrentA = currentMA / 1000.0f;
    // E/F and charger faults are observable states, but do not prove vehicle
    // presence or absence of charging. Preserve unknown for those booleans.
    sample.vehicleKnown = sample.iecState < 4 && sample.chargingState < 4;
    sample.vehicleConnected = sample.iecState != 0;
    sample.chargingKnown = sample.vehicleKnown;
    sample.charging = sample.chargingState == 3;
    if (sample.meterAvailable) {
        if (!meter) { return false; }
        sample.powerW = Modbus::decodeFloat32(meter);
        sample.sessionEnergyKWh = Modbus::decodeFloat32(meter + 12);
        if (!std::isfinite(sample.powerW) || sample.powerW < 0
                || !std::isfinite(sample.sessionEnergyKWh) || sample.sessionEnergyKWh < 0) { return false; }
    }
    return true;
}

void State::reset(bool configured, bool enabled)
{
    _state = {};
    _lastResponseMs = 0;
    _state.configured = configured;
    _state.enabled = enabled;
}

void State::apply(Sample const& sample, Modbus::Result result, uint32_t now)
{
    if (!_state.enabled) { return; }
    _state.error = result.error;
    _state.exception = result.exception;
    _state.reachable = result.responded;
    if (result.responded) { _lastResponseMs = now; }
    _state.valid = static_cast<bool>(result);
    if (!result) {
        ++_state.failedPolls;
        // Retain last measurements/timestamp for diagnostics; API gates all
        // operational values by validity. Failure never manufactures zero/false.
        return;
    }
    _state.sample = sample;
    _state.hasUpdate = true;
    _state.lastSuccessMs = now;
    ++_state.successfulPolls;
}

Snapshot State::snapshot(uint32_t now) const
{
    auto result = _state;
    result.ageMs = result.hasUpdate ? now - result.lastSuccessMs : 0;
    result.stale = result.hasUpdate && result.ageMs >= StaleTimeoutMs;
    result.valid = result.enabled && result.valid && !result.stale;
    result.reachable = result.enabled && result.reachable && now - _lastResponseMs < StaleTimeoutMs;
    return result;
}

} // namespace Hems::Warp
