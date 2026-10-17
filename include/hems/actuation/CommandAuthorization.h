// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/Control.h>
#include <hems/actuation/Mailbox.h>

namespace Hems::Actuation {

using AuthorizationCallback = bool (*)(void* context, Command const& command);

} // namespace Hems::Actuation
