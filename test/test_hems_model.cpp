// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/constraints/Constraints.h>
#include <hems/model/Normalize.h>
#include <cassert>
#include <iostream>
#include <optional>

namespace {

void testFreshStorage()
{
    Hems::Storage::Snapshot storage;
    storage.configured = true;
    storage.enabled = true;
    storage.reachable = true;
    storage.valid = true;
    storage.hasUpdate = true;
    storage.ageMs = 100;
    storage.sample.socPct = { true, 52.5 };
    storage.sample.acPowerW = { true, -740.0 };
    storage.sample.charging = { true, false };
    storage.sample.discharging = { true, true };
    storage.sample.faultActive = { true, false };

    auto state = Hems::Model::normalize({}, storage);
    assert(state.storage.device.configured);
    assert(state.storage.device.enabled);
    assert(state.storage.device.reachable);
    assert(state.storage.socPct.usable());
    assert(state.storage.socPct.value == 52.5);
    assert(state.storage.acPowerW.usable());
    assert(state.storage.acPowerW.value == -740.0);
    assert(state.storage.charging.usable());
    assert(!state.storage.charging.value);
    assert(state.storage.discharging.usable());
    assert(state.storage.discharging.value);
    assert(state.storage.faultActive.usable());
    assert(!state.storage.faultActive.value);
    assert(!state.storage.alarmActive.known());
}

void testFreshZeroAndFalseRemainValid()
{
    Hems::Storage::Snapshot storage;
    storage.configured = true;
    storage.enabled = true;
    storage.reachable = true;
    storage.valid = true;
    storage.hasUpdate = true;
    storage.sample.socPct = { true, 0.0 };
    storage.sample.acPowerW = { true, 0.0 };
    storage.sample.charging = { true, false };
    storage.sample.discharging = { true, false };
    storage.sample.faultActive = { true, false };
    storage.sample.alarmActive = { true, false };

    auto state = Hems::Model::normalize({}, storage);
    assert(state.storage.socPct.usable());
    assert(state.storage.socPct.value == 0.0);
    assert(state.storage.acPowerW.usable());
    assert(state.storage.acPowerW.value == 0.0);
    assert(state.storage.charging.usable());
    assert(!state.storage.charging.value);
    assert(state.storage.discharging.usable());
    assert(!state.storage.discharging.value);
    assert(state.storage.faultActive.usable());
    assert(!state.storage.faultActive.value);
    assert(state.storage.alarmActive.usable());
    assert(!state.storage.alarmActive.value);
}

void testFailedPollDoesNotReuseOperationalValues()
{
    Hems::Storage::Snapshot storage;
    storage.configured = true;
    storage.enabled = true;
    storage.reachable = false;
    storage.valid = false;
    storage.hasUpdate = true;
    storage.ageMs = 500;
    storage.sample.socPct = { true, 88.0 };
    storage.sample.discharging = { true, true };

    auto state = Hems::Model::normalize({}, storage);
    assert(!state.storage.socPct.known());
    assert(!state.storage.discharging.known());
}

void testStaleValuesRemainDiagnosticOnly()
{
    Hems::Storage::Snapshot storage;
    storage.configured = true;
    storage.enabled = true;
    storage.stale = true;
    storage.hasUpdate = true;
    storage.ageMs = 20000;
    storage.sample.socPct = { true, 41.0 };

    auto state = Hems::Model::normalize({}, storage);
    assert(state.storage.socPct.known());
    assert(!state.storage.socPct.usable());
    assert(state.storage.socPct.quality == Hems::Model::Quality::Stale);
    assert(state.storage.socPct.value == 41.0);
}

void testEvseUnknownsAndFalseStayDistinct()
{
    Hems::Warp::Snapshot warp;
    warp.configured = true;
    warp.enabled = true;
    warp.reachable = true;
    warp.valid = true;
    warp.hasUpdate = true;
    warp.sample.allowedCurrentA = 6.0f;
    warp.sample.vehicleKnown = true;
    warp.sample.vehicleConnected = false;
    warp.sample.chargingKnown = true;
    warp.sample.charging = false;
    warp.sample.meterAvailable = false;

    auto state = Hems::Model::normalize(warp, {});
    assert(state.evse.vehicleConnected.usable());
    assert(!state.evse.vehicleConnected.value);
    assert(state.evse.charging.usable());
    assert(!state.evse.charging.value);
    assert(state.evse.allowedCurrentA.usable());
    assert(state.evse.allowedCurrentA.value == 6.0);
    assert(!state.evse.powerW.known());
}

void testFailedEvsePollDoesNotReuseOperationalValues()
{
    Hems::Warp::Snapshot warp;
    warp.configured = true;
    warp.enabled = true;
    warp.reachable = false;
    warp.valid = false;
    warp.hasUpdate = true;
    warp.sample.allowedCurrentA = 16.0f;
    warp.sample.vehicleKnown = true;
    warp.sample.vehicleConnected = true;
    warp.sample.chargingKnown = true;
    warp.sample.charging = true;
    warp.sample.meterAvailable = true;
    warp.sample.powerW = 11000.0f;

    auto state = Hems::Model::normalize(warp, {});
    assert(!state.evse.allowedCurrentA.known());
    assert(!state.evse.vehicleConnected.known());
    assert(!state.evse.charging.known());
    assert(!state.evse.powerW.known());
}

void testStaleEvseValuesRemainDiagnosticOnly()
{
    Hems::Warp::Snapshot warp;
    warp.configured = true;
    warp.enabled = true;
    warp.reachable = true;
    warp.valid = true;
    warp.stale = true;
    warp.hasUpdate = true;
    warp.ageMs = 20000;
    warp.sample.allowedCurrentA = 10.0f;
    warp.sample.vehicleKnown = true;
    warp.sample.vehicleConnected = true;
    warp.sample.chargingKnown = true;
    warp.sample.charging = false;

    auto state = Hems::Model::normalize(warp, {});
    assert(state.evse.allowedCurrentA.known());
    assert(!state.evse.allowedCurrentA.usable());
    assert(state.evse.allowedCurrentA.quality == Hems::Model::Quality::Stale);
    assert(state.evse.vehicleConnected.known());
    assert(!state.evse.vehicleConnected.usable());
    assert(state.evse.vehicleConnected.value);
    assert(state.evse.charging.known());
    assert(!state.evse.charging.usable());
    assert(!state.evse.charging.value);
}

void testGridPowerPreservesZeroUnknownAndStale()
{
    auto freshZero = Hems::Model::normalizeGridPower(0.0f, true, true);
    assert(freshZero.usable());
    assert(freshZero.value == 0.0);

    auto stale = Hems::Model::normalizeGridPower(815.0f, false, true);
    assert(stale.known());
    assert(!stale.usable());
    assert(stale.quality == Hems::Model::Quality::Stale);
    assert(stale.value == 815.0);

    auto unknown = Hems::Model::normalizeGridPower(std::nullopt, false, false);
    assert(!unknown.known());

    auto valueWithoutUpdate = Hems::Model::normalizeGridPower(123.0f, true, false);
    assert(!valueWithoutUpdate.known());
}

void testConstraintComposition()
{
    Hems::Constraints::ConstraintSet first;
    first.storage.dischargeAllowed = false;
    first.storage.maxChargePower = { true, 1800.0 };

    Hems::Constraints::ConstraintSet second;
    second.storage.chargeAllowed = false;
    second.storage.maxChargePower = { true, 1200.0 };
    second.storage.maxDischargePower = { true, 900.0 };

    auto combined = Hems::Constraints::combine(first, second);
    assert(!combined.storage.chargeAllowed);
    assert(!combined.storage.dischargeAllowed);
    assert(combined.storage.maxChargePower.active);
    assert(combined.storage.maxChargePower.valueW == 1200.0);
    assert(combined.storage.maxDischargePower.active);
    assert(combined.storage.maxDischargePower.valueW == 900.0);
}

void testConstraintCompositionIsOrderIndependent()
{
    Hems::Constraints::ConstraintSet first;
    first.storage.dischargeAllowed = false;
    first.storage.maxChargePower = { true, 900.0 };

    Hems::Constraints::ConstraintSet second;
    second.storage.chargeAllowed = false;
    second.storage.maxChargePower = { true, 1200.0 };
    second.storage.maxDischargePower = { true, 700.0 };

    auto forward = Hems::Constraints::combine(first, second);
    auto reverse = Hems::Constraints::combine(second, first);

    assert(forward.storage.chargeAllowed == reverse.storage.chargeAllowed);
    assert(forward.storage.dischargeAllowed == reverse.storage.dischargeAllowed);
    assert(forward.storage.maxChargePower.active == reverse.storage.maxChargePower.active);
    assert(forward.storage.maxChargePower.valueW == reverse.storage.maxChargePower.valueW);
    assert(forward.storage.maxDischargePower.active == reverse.storage.maxDischargePower.active);
    assert(forward.storage.maxDischargePower.valueW == reverse.storage.maxDischargePower.valueW);
}

} // namespace

int main()
{
    testFreshStorage();
    testFreshZeroAndFalseRemainValid();
    testFailedPollDoesNotReuseOperationalValues();
    testStaleValuesRemainDiagnosticOnly();
    testEvseUnknownsAndFalseStayDistinct();
    testFailedEvsePollDoesNotReuseOperationalValues();
    testStaleEvseValuesRemainDiagnosticOnly();
    testGridPowerPreservesZeroUnknownAndStale();
    testConstraintComposition();
    testConstraintCompositionIsOrderIndependent();
    std::cout << "Normalized HEMS state and constraint tests passed\n";
}
