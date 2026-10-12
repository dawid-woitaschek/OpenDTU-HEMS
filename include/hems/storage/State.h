// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/modbus/Protocol.h>
#include <hems/storage/Telemetry.h>
#include <cstdint>

namespace Hems::Storage {

constexpr uint32_t PollIntervalMs = 3000;
constexpr uint32_t StaleTimeoutMs = 15000;

struct Snapshot {
    Sample sample;
    bool configured = false;
    bool enabled = false;
    bool profileLoaded = false;
    bool reachable = false;
    bool valid = false;
    bool stale = false;
    bool hasUpdate = false;
    uint32_t lastSuccessMs = 0;
    uint32_t ageMs = 0;
    uint32_t successfulPolls = 0;
    uint32_t failedPolls = 0;
    Modbus::Error error = Modbus::Error::None;
    uint8_t exception = 0;
    uint16_t port = 0;
    uint8_t unit = 0;
};

class State {
public:
    void reset(bool configured, bool enabled, bool profileLoaded, uint16_t port, uint8_t unit);
    void apply(Sample const& sample, Modbus::Result result, uint32_t now);
    Snapshot snapshot(uint32_t now) const;

private:
    Snapshot _state;
    uint32_t _lastResponseMs = 0;
};

} // namespace Hems::Storage
