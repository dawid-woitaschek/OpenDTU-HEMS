// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdint>
#include <hems/policies/EvProtection.h>

using Hems::Model::EvseState;
using Hems::Model::Signal;
using Hems::Policies::EvProtectionDebounceMs;
using Hems::Policies::EvProtectionPolicy;
using Hems::Policies::EvProtectionState;

namespace {

EvseState evseWithCharging(Signal<bool> charging)
{
    EvseState state;
    state.device.configured = true;
    state.device.enabled = true;
    state.device.reachable = true;
    state.device.hasUpdate = charging.known();
    state.charging = charging;
    return state;
}

} // namespace

int main()
{
    EvProtectionPolicy policy;

    auto snapshot = policy.snapshot(0);
    assert(snapshot.state == EvProtectionState::Inactive);
    assert(!snapshot.chargingKnown);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 1000);
    snapshot = policy.snapshot(1000);
    assert(snapshot.state == EvProtectionState::Inactive);
    assert(snapshot.chargingKnown);
    assert(!snapshot.charging);

    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 2000);
    snapshot = policy.snapshot(2000);
    assert(snapshot.state == EvProtectionState::Waiting);
    assert(snapshot.chargingKnown);
    assert(snapshot.charging);
    assert(snapshot.conditionAgeMs == 0);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 2000 + EvProtectionDebounceMs - 1);
    snapshot = policy.snapshot(2000 + EvProtectionDebounceMs - 1);
    assert(snapshot.state == EvProtectionState::Waiting);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 2000 + EvProtectionDebounceMs);
    snapshot = policy.snapshot(2000 + EvProtectionDebounceMs);
    assert(snapshot.state == EvProtectionState::WouldBlockStorageDischarge);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::stale(true)), 2000 + EvProtectionDebounceMs + 1);
    snapshot = policy.snapshot(2000 + EvProtectionDebounceMs + 1);
    assert(snapshot.state == EvProtectionState::Inactive);
    assert(!snapshot.chargingKnown);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 500000);
    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 500100);
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 500200);
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 500200 + EvProtectionDebounceMs - 1);
    assert(policy.snapshot(500200 + EvProtectionDebounceMs - 1).state == EvProtectionState::Waiting);

    policy.reset();
    uint32_t rolloverStart = 0xffffff00U;
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), rolloverStart);
    uint32_t rolloverDone = rolloverStart + EvProtectionDebounceMs;
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), rolloverDone);
    snapshot = policy.snapshot(rolloverDone);
    assert(snapshot.state == EvProtectionState::WouldBlockStorageDischarge);
    assert(snapshot.conditionAgeMs == EvProtectionDebounceMs);

    return 0;
}
