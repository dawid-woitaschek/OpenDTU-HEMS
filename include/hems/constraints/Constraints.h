// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Hems::Constraints {

struct UpperLimit {
    bool active = false;
    double valueW = 0;
};

struct StorageConstraints {
    bool chargeAllowed = true;
    bool dischargeAllowed = true;
    UpperLimit maxChargePower;
    UpperLimit maxDischargePower;
};

struct ConstraintSet {
    StorageConstraints storage;
};

ConstraintSet combine(ConstraintSet const& first, ConstraintSet const& second);

} // namespace Hems::Constraints
