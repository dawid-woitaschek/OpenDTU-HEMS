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

    bool reset()
    {
        if (_session.ownsControl()) {
            _orphaned = true;
            return false;
        }
        _session.reset();
        _orphaned = false;
        return true;
    }
    // Called only by the worker after authenticated exact-record operator
    // attestation and successful durable clearing; never a normal reset path.
    void confirmOperatorRelease() { _session.reset(); _orphaned = false; }
    bool ownsControl() const { return _session.ownsControl(); }
    bool ownershipOrphaned() const { return _orphaned; }

private:
    Actuation::ProfileSession _session;
    bool _orphaned = false;
};

} // namespace Hems::Evse
