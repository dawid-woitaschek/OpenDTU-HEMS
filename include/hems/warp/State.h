// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/modbus/Observations.h>

#include <hems/modbus/Protocol.h>

namespace Hems::Warp {

constexpr uint32_t PollIntervalMs = 3000;
constexpr uint32_t StaleTimeoutMs = 15000;

struct Sample {
    Modbus::Observations raw;
    uint32_t tableVersion = 0;
    uint32_t iecState = 0;
    uint32_t chargingState = 0;
    float allowedCurrentA = 0;
    float powerW = 0;
    float sessionEnergyKWh = 0;
    bool meterAvailable = false;
    bool vehicleKnown = false;
    bool vehicleConnected = false;
    bool chargingKnown = false;
    bool charging = false;
};

bool decode(Sample& sample, uint8_t const* evse, uint8_t const* meter);

struct Snapshot {
    Sample sample;
    bool configured = false;
    bool enabled = false; // Effective: HEMS Monitor AND provider enabled.
    bool profileConfigured = false;
    bool profileLoaded = false;
    bool profileDriven = false; // Imported profile is the active read path.
    bool reachable = false;
    bool valid = false;
    bool stale = false;
    bool hasUpdate = false;
    uint32_t lastSuccessMs = 0; // Device uptime, not wall-clock time.
    uint32_t ageMs = 0;
    uint32_t successfulPolls = 0;
    uint32_t failedPolls = 0;
    Modbus::Error error = Modbus::Error::None;
    uint8_t exception = 0;
};

class State {
public:
    void reset(bool configured, bool enabled);
    void apply(Sample const& sample, Modbus::Result result, uint32_t now);
    Snapshot snapshot(uint32_t now) const;

private:
    Snapshot _state;
    uint32_t _lastResponseMs = 0;
};

} // namespace Hems::Warp
