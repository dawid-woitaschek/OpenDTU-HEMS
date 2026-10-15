// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Readiness.h>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace Hems::Actuation;

int main()
{
    ReadinessInput readyInput;
    readyInput.globalActive = true;
    readyInput.permissionGranted = true;
    readyInput.configured = true;
    readyInput.enabled = true;
    readyInput.profileControllable = true;
    readyInput.reachable = true;
    readyInput.telemetryFresh = true;
    readyInput.faulted = false;

    auto ready = evaluateReadiness(readyInput);
    assert(ready.allowed());
    assert(ready.readyForActive());
    assert(ready.blockers == 0);

    readyInput.globalActive = false;
    auto monitorOnly = evaluateReadiness(readyInput);
    assert(!monitorOnly.allowed());
    assert(monitorOnly.readyForActive());
    assert(monitorOnly.has(ReadinessBlocker::GlobalModeInactive));
    assert(monitorOnly.blockers == readinessBit(ReadinessBlocker::GlobalModeInactive));

    ReadinessInput blocked;
    blocked.globalActive = false;
    blocked.permissionGranted = false;
    blocked.configured = false;
    blocked.enabled = false;
    blocked.profileControllable = false;
    blocked.reachable = false;
    blocked.telemetryFresh = false;
    blocked.faulted = true;
    auto allBlocked = evaluateReadiness(blocked);
    assert(!allBlocked.allowed());
    assert(!allBlocked.readyForActive());
    assert(allBlocked.has(ReadinessBlocker::GlobalModeInactive));
    assert(allBlocked.has(ReadinessBlocker::DevicePermissionMissing));
    assert(allBlocked.has(ReadinessBlocker::DeviceNotConfigured));
    assert(allBlocked.has(ReadinessBlocker::DeviceNotEnabled));
    assert(allBlocked.has(ReadinessBlocker::ProfileNotControllable));
    assert(allBlocked.has(ReadinessBlocker::DeviceUnreachable));
    assert(allBlocked.has(ReadinessBlocker::TelemetryNotFresh));
    assert(allBlocked.has(ReadinessBlocker::DeviceFault));

    assert(!std::strcmp(readinessBlockerName(ReadinessBlocker::GlobalModeInactive), "global_mode_inactive"));
    assert(!std::strcmp(readinessBlockerName(ReadinessBlocker::DevicePermissionMissing), "device_permission_missing"));
    assert(!std::strcmp(readinessBlockerName(ReadinessBlocker::ProfileNotControllable), "profile_not_controllable"));
    assert(!std::strcmp(readinessBlockerName(ReadinessBlocker::TelemetryNotFresh), "telemetry_not_fresh"));
    assert(!std::strcmp(readinessBlockerName(ReadinessBlocker::DeviceFault), "device_fault"));

    std::cout << "Actuation readiness tests passed\n";
}
