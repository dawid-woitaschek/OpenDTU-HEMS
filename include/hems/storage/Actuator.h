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

    // A provider generation change must never silently forget that a previous
    // endpoint may still be under remote control. Once orphaned, the actuator
    // rejects normal commands until retained-context cleanup resolves ownership.
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

} // namespace Hems::Storage
