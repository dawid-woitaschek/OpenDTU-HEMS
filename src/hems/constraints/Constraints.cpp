// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/constraints/Constraints.h>
#include <algorithm>

namespace {

Hems::Constraints::UpperLimit combineLimit(Hems::Constraints::UpperLimit const& first,
        Hems::Constraints::UpperLimit const& second)
{
    if (!first.active) { return second; }
    if (!second.active) { return first; }
    return { true, std::min(first.valueW, second.valueW) };
}

} // namespace

namespace Hems::Constraints {

ConstraintSet combine(ConstraintSet const& first, ConstraintSet const& second)
{
    ConstraintSet result;
    result.storage.chargeAllowed = first.storage.chargeAllowed && second.storage.chargeAllowed;
    result.storage.dischargeAllowed = first.storage.dischargeAllowed && second.storage.dischargeAllowed;
    result.storage.maxChargePower = combineLimit(first.storage.maxChargePower, second.storage.maxChargePower);
    result.storage.maxDischargePower = combineLimit(first.storage.maxDischargePower, second.storage.maxDischargePower);
    return result;
}

} // namespace Hems::Constraints
