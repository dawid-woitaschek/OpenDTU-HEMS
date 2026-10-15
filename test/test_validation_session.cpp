// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/validation/Session.h>
#include <hems/actuation/Readiness.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace Hems;
static void randomBytes(void* out, size_t n) {
    static uint8_t value = 1; auto* bytes = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < n; ++i) { bytes[i] = value++; }
}
int main() {
    Validation::Session s; char token[33] {}, previous[33];
    assert(!s.snapshot(0).active && !s.accept(token, 0));
    assert(!s.start(0, Validation::MaxTtlMs + 1, randomBytes, token));
    assert(s.start(0, 5000, randomBytes, token)); strcpy(previous, token);
    assert(strlen(token) == 32 && !s.start(0, 5000, randomBytes, token));
    assert(!s.accept("bad", 0)); assert(s.accept(token, 0));
    assert(!s.accept(token, 999) && s.accept(token, 1000));
    assert(s.snapshot(4999).active && s.snapshot(4999).remainingMs == 1);
    assert(!s.accept(token, 5000) && !s.snapshot(5000).active);
    assert(!s.start(5000, 5000, randomBytes, token)); // service expiry first
    assert(s.expire(5000) && !s.expire(5001));
    assert(s.start(5000, 5000, randomBytes, token) && strcmp(previous, token));
    assert(!s.revoke(previous) && !s.accept(previous, 6000));
    assert(s.revoke(token) && !s.accept(token, 6000));
    assert(s.snapshot(6000).state == Validation::State::Revoked);
    assert(s.start(0xfffffff0u, 5000, randomBytes, token));
    assert(s.accept(token, 0x10) && s.snapshot(0x10).remainingMs == 4968);
    assert(s.expire(6000));
    // The scoped maintenance gate cannot bypass recovery or permission blockers.
    Actuation::ReadinessInput input { true, true, true, true, true, true, true };
    input.validationInactive = true;
    assert(!Actuation::evaluateReadiness(input).allowed());
    input.validationInactive = false; input.recoveryUnsupported = true;
    assert(!Actuation::evaluateReadiness(input).allowed());
    input.recoveryUnsupported = false; input.recoveryPending = true;
    assert(!Actuation::evaluateReadiness(input).allowed());
    input.recoveryPending = false; input.permissionGranted = false;
    assert(!Actuation::evaluateReadiness(input).allowed());
    std::cout << "Validation session token, TTL, revocation, rate and common readiness tests passed\n";
}
