// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Readiness.h>
#include <cassert>
using namespace Hems::Actuation;
int main()
{
    ReadinessInput input;
    input.globalActive = input.permissionGranted = input.configured = input.enabled = true;
    input.profileControllable = input.reachable = input.telemetryFresh = true;
    assert(evaluateReadiness(input).allowed());
    input.commandBusy = true; assert(evaluateReadiness(input).has(ReadinessBlocker::CommandBusy));
    input.commandBusy = false; input.telemetryFresh = false;
    assert(evaluateReadiness(input).has(ReadinessBlocker::TelemetryNotFresh));
    input.telemetryFresh = true; input.reachable = false;
    assert(evaluateReadiness(input).has(ReadinessBlocker::DeviceUnreachable));
    input.reachable = true; assert(evaluateReadiness(input).allowed());
    input.permissionGranted = false; assert(!evaluateReadiness(input).allowed());
    input.permissionGranted = true; input.globalActive = false;
    assert(evaluateReadiness(input).readyForActive() && !evaluateReadiness(input).allowed());
}
