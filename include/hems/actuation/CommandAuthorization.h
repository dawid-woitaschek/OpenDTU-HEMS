// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/Control.h>
#include <hems/actuation/Mailbox.h>

namespace Hems::Actuation {

using AuthorizationCallback = bool (*)(void* context, ControlTarget target);

inline bool commandIsActuating(Command const& command)
{
    switch (command.action) {
        case Action::SetCurrentA:
        case Action::SetChargePowerW:
        case Action::SetDischargePowerW:
        case Action::SetTargetSocPct:
        case Action::Stop:
            return true;
        case Action::Release:
            return false;
    }
    return true;
}

} // namespace Hems::Actuation
