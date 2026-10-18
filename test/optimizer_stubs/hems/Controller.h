// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/model/State.h>
#include <hems/constraints/Constraints.h>
#include <hems/policies/EvProtection.h>
// Deliberately no actuation methods. Production shadow service must link using read-only snapshots.
inline Hems::Model::SystemState optimizerState;
inline Hems::Constraints::ConstraintSet optimizerConstraints;
inline struct TestReadOnlyController {
    uint64_t economicContextRevision() const { return 0; }
    Hems::Model::SystemState getNormalizedState() const { return optimizerState; }
    Hems::Constraints::ConstraintSet getConstraints() const { return optimizerConstraints; }
    Hems::Policies::EvProtectionSnapshot getEvProtectionShadowSnapshot() const {
        Hems::Policies::EvProtectionSnapshot s; s.proposedConstraints=optimizerConstraints; return s;
    }
} HemsController;
