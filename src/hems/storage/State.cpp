// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/State.h>

namespace Hems::Storage {

void State::reset(bool configured, bool enabled, bool profileLoaded, uint16_t port, uint8_t unit)
{
    _state = {};
    _lastResponseMs = 0;
    _state.configured = configured;
    _state.enabled = enabled;
    _state.profileLoaded = profileLoaded;
    _state.port = port;
    _state.unit = unit;
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

} // namespace Hems::Storage
