// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Actuator.h>

namespace Hems::Storage {
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
    return Profiles::hasCapability(profile, Profiles::Capability::Storage);
}

} // namespace

Actuation::CommandResult Actuator::setChargePower(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile, double watts)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.execute(io, profile, "charge_power_w", true, watts);
}

Actuation::CommandResult Actuator::setDischargePower(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile, double watts)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.execute(io, profile, "discharge_power_w", true, watts);
}

Actuation::CommandResult Actuator::setTargetSoc(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile, double percent)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.execute(io, profile, "target_soc_pct", true, percent);
}

Actuation::CommandResult Actuator::stop(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    return _session.execute(io, profile, "stop");
}

Actuation::CommandResult Actuator::releaseControl(Actuation::RegisterIo const& io,
        Profiles::Profile const& profile)
{
    if (_orphaned) { return orphaned(); }
    if (!compatible(profile)) { return incompatible(); }
    // An explicit release is deliberately forced. After a reboot we may no
    // longer remember ownership while the device can still be in remote mode.
    return _session.release(io, profile, true);
}

} // namespace Hems::Storage
