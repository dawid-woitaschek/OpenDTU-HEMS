// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/evse/Actuator.h>

namespace Hems::Evse {
namespace {

Actuation::CommandResult incompatible()
{
    Actuation::CommandResult result;
    result.error = Actuation::CommandError::PlanningFailed;
    result.stage = Actuation::CommandStage::Operation;
    result.planning.error = Profiles::ControlPlanError::ControlUnavailable;
    return result;
}

Actuation::CommandResult orphaned()
{
    Actuation::CommandResult result;
    result.error = Actuation::CommandError::OwnershipOrphaned;
    return result;
}

bool compatible(Profiles::Profile const& profile)
{
    return Profiles::hasCapability(profile, Profiles::Capability::Evse);
}

} // namespace

Actuation::CommandResult Actuator::setCurrent(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile, double amps)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.execute(io, profile, "set_current_a", true, amps);
}

Actuation::CommandResult Actuator::stop(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.execute(io, profile, "stop_charging");
}

Actuation::CommandResult Actuator::releaseControl(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.release(io, profile, true);
}

} // namespace Hems::Evse
