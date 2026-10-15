// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace Hems::Actuation {

enum class ReadinessBlocker : uint16_t {
    None = 0,
    GlobalModeInactive = 1u << 0,
    DevicePermissionMissing = 1u << 1,
    DeviceNotConfigured = 1u << 2,
    DeviceNotEnabled = 1u << 3,
    ProfileNotControllable = 1u << 4,
    DeviceUnreachable = 1u << 5,
    TelemetryNotFresh = 1u << 6,
    DeviceFault = 1u << 7,
};

constexpr uint16_t readinessBit(ReadinessBlocker blocker)
{
    return static_cast<uint16_t>(blocker);
}

struct ReadinessInput {
    bool globalActive = false;
    bool permissionGranted = false;
    bool configured = false;
    bool enabled = false;
    bool profileControllable = false;
    bool reachable = false;
    bool telemetryFresh = false;
    bool faulted = false;
};

struct Readiness {
    uint16_t blockers = 0;

    bool has(ReadinessBlocker blocker) const
    {
        return (blockers & readinessBit(blocker)) != 0;
    }

    bool allowed() const { return blockers == 0; }

    // Useful while the project is still Monitor-only: every device/runtime
    // prerequisite is satisfied except the deliberately absent global Active mode.
    bool readyForActive() const
    {
        return (blockers & ~readinessBit(ReadinessBlocker::GlobalModeInactive)) == 0;
    }
};

Readiness evaluateReadiness(ReadinessInput const& input);
char const* readinessBlockerName(ReadinessBlocker blocker);

} // namespace Hems::Actuation
