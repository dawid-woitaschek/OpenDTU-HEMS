// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/actuation/ProfileSession.h>

namespace Hems::Evse {

class Actuator {
public:
    Actuation::CommandResult setCurrent(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile, double amps);
    Actuation::CommandResult stop(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile);
    Actuation::CommandResult releaseControl(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile);
    Actuation::CommandResult releaseRetainedControl(Actuation::RegisterIo const& io,
            Profiles::Profile const& profile);

    void reset() { _session.reset(); }
    bool ownsControl() const { return _session.ownsControl(); }

private:
    Actuation::ProfileSession _session;
};

} // namespace Hems::Evse
