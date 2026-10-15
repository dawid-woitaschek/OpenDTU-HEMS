// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/actuation/Executor.h>
#include <hems/modbus/Client.h>

namespace Hems::Actuation {

RegisterIo clientRegisterIo(Modbus::Client& client);

} // namespace Hems::Actuation
