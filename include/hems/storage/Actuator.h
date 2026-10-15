// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/actuation/ProfileSession.h>

namespace Hems::Storage {

class Actuator {
public:
    Actuation::CommandResult setChargePower(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile, double watts);
    Actuation::CommandResult setDischargePower(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile, double watts);
    Actuation::CommandResult setTargetSoc(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile, double percent);
    Actuation::CommandResult stop(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile);
    Actuation::CommandResult releaseControl(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile);

    // Safety cleanup only: the provider retained the exact old endpoint/profile
    // and may use this after normal authorization has already been revoked.
    Actuation::CommandResult releaseRetainedControl(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile);

    void reset() { _session.reset(); }
    bool ownsControl() const { return _session.ownsControl(); }

private:
    Actuation::ProfileSession _session;
};

} // namespace Hems::Storage
