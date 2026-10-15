// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Readiness.h>

namespace Hems::Actuation {

Readiness evaluateReadiness(ReadinessInput const& input)
{
    Readiness result;
    auto add = [&result](ReadinessBlocker blocker, bool condition) {
        if (condition) { result.blockers |= readinessBit(blocker); }
    };

    add(ReadinessBlocker::GlobalModeInactive, !input.globalActive);
    add(ReadinessBlocker::DevicePermissionMissing, !input.permissionGranted);
    add(ReadinessBlocker::DeviceNotConfigured, !input.configured);
    add(ReadinessBlocker::DeviceNotEnabled, !input.enabled);
    add(ReadinessBlocker::ProfileNotControllable, !input.profileControllable);
    add(ReadinessBlocker::DeviceUnreachable, !input.reachable);
    add(ReadinessBlocker::TelemetryNotFresh, !input.telemetryFresh);
    add(ReadinessBlocker::DeviceFault, input.faulted);
    return result;
}

char const* readinessBlockerName(ReadinessBlocker blocker)
{
    switch (blocker) {
        case ReadinessBlocker::GlobalModeInactive: return "global_mode_inactive";
        case ReadinessBlocker::DevicePermissionMissing: return "device_permission_missing";
        case ReadinessBlocker::DeviceNotConfigured: return "device_not_configured";
        case ReadinessBlocker::DeviceNotEnabled: return "device_not_enabled";
        case ReadinessBlocker::ProfileNotControllable: return "profile_not_controllable";
        case ReadinessBlocker::DeviceUnreachable: return "device_unreachable";
        case ReadinessBlocker::TelemetryNotFresh: return "telemetry_not_fresh";
        case ReadinessBlocker::DeviceFault: return "device_fault";
        case ReadinessBlocker::None:
        default: return "none";
    }
}

} // namespace Hems::Actuation
