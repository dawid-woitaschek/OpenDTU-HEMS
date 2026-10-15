// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/ClientIo.h>

namespace Hems::Actuation {
namespace {

Modbus::Result writeHolding(void* context, uint16_t address,
        uint16_t count, uint16_t const* values)
{
    return static_cast<Modbus::Client*>(context)->writeHoldingRegisters(address, count, values);
}

Modbus::Result readHolding(void* context, uint16_t address,
        uint16_t count, uint8_t* payload)
{
    return static_cast<Modbus::Client*>(context)->readHoldingRegisters(address, count, payload);
}

} // namespace

RegisterIo clientRegisterIo(Modbus::Client& client)
{
    return { &client, writeHolding, readHolding };
}

} // namespace Hems::Actuation
