// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdint>
#include <hems/policies/EvProtection.h>

using Hems::Model::EvseState;
using Hems::Model::Signal;
using Hems::Policies::EvProtectionStopDebounceMs;
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

    // Fresh charging blocks storage discharge immediately. The debounce belongs
    // to release after charging stops, not to detecting an active EV session.
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 2000);
    snapshot = policy.snapshot(2000);
    assert(snapshot.state == EvProtectionState::WouldBlockStorageDischarge);
    assert(snapshot.chargingKnown && snapshot.charging);
    assert(snapshot.conditionAgeMs == 0);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    // A fresh false starts the stop-debounce window but keeps protection active.
    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 3000);
    snapshot = policy.snapshot(3000);
    assert(snapshot.state == EvProtectionState::Waiting);
    assert(snapshot.chargingKnown && !snapshot.charging);
    assert(snapshot.conditionAgeMs == 0);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 3000 + EvProtectionStopDebounceMs - 1);
    snapshot = policy.snapshot(3000 + EvProtectionStopDebounceMs - 1);
    assert(snapshot.state == EvProtectionState::Waiting);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    // Charging resumes before expiry: return immediately to protected state.
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), 50000);
    snapshot = policy.snapshot(50000);
    assert(snapshot.state == EvProtectionState::WouldBlockStorageDischarge);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    // Unknown/stale telemetry must never be interpreted as "not charging".
    policy.update(evseWithCharging(Signal<bool>::stale(true)), 50001);
    snapshot = policy.snapshot(50001);
    assert(snapshot.state == EvProtectionState::WouldBlockStorageDischarge);
    assert(!snapshot.chargingKnown);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    // Once fresh false returns, a complete continuous stop window is required.
    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 60000);
    assert(policy.snapshot(60000).state == EvProtectionState::Waiting);
    policy.update(evseWithCharging(Signal<bool>::stale(false)), 60001);
    snapshot = policy.snapshot(60001);
    assert(snapshot.state == EvProtectionState::Waiting);
    assert(!snapshot.chargingKnown);
    assert(!snapshot.proposedConstraints.storage.dischargeAllowed);

    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 70000);
    assert(policy.snapshot(70000).state == EvProtectionState::Waiting);
    policy.update(evseWithCharging(Signal<bool>::fresh(false)), 70000 + EvProtectionStopDebounceMs);
    snapshot = policy.snapshot(70000 + EvProtectionStopDebounceMs);
    assert(snapshot.state == EvProtectionState::Inactive);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    // Default-constructed telemetry is Unknown while already inactive.
    policy.update(evseWithCharging(Signal<bool>{}), 80000);
    snapshot = policy.snapshot(80000);
    assert(snapshot.state == EvProtectionState::Inactive);
    assert(!snapshot.chargingKnown);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    // Stop-debounce arithmetic is rollover-safe.
    policy.reset();
    uint32_t rolloverStart = 0xffffff00U;
    policy.update(evseWithCharging(Signal<bool>::fresh(true)), rolloverStart - 1);
    policy.update(evseWithCharging(Signal<bool>::fresh(false)), rolloverStart);
    uint32_t rolloverDone = rolloverStart + EvProtectionStopDebounceMs;
    policy.update(evseWithCharging(Signal<bool>::fresh(false)), rolloverDone);
    snapshot = policy.snapshot(rolloverDone);
    assert(snapshot.state == EvProtectionState::Inactive);
    assert(snapshot.proposedConstraints.storage.dischargeAllowed);

    return 0;
}
