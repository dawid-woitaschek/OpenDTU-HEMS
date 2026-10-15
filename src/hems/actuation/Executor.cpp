// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Executor.h>

namespace Hems::Actuation {

char const* executionErrorName(ExecutionError error)
{
    switch (error) {
        case ExecutionError::None: return "None";
        case ExecutionError::InvalidIo: return "InvalidIo";
        case ExecutionError::WriteFailed: return "WriteFailed";
        case ExecutionError::ReadbackFailed: return "ReadbackFailed";
        case ExecutionError::VerificationFailed: return "VerificationFailed";
    }
    return "InvalidIo";
}

ExecutionResult execute(RegisterIo const& io, Profiles::ControlPlan const& plan,
        bool verifyReadback)
{
    if (!io.writeHoldingRegisters || (verifyReadback && !io.readHoldingRegisters)
            || plan.count > Profiles::MaxControlWrites) {
        return { ExecutionError::InvalidIo };
    }

    for (uint8_t i = 0; i < plan.count; ++i) {
        auto const& write = plan.writes[i];
        if (write.count == 0 || write.count > Modbus::MaxWriteRegisters) {
            return { ExecutionError::InvalidIo, {}, i };
        }

        auto result = io.writeHoldingRegisters(io.context, write.address,
                write.count, write.values);
        if (!result) { return { ExecutionError::WriteFailed, result, i }; }
        if (!verifyReadback) { continue; }

        uint8_t payload[Modbus::MaxWriteRegisters * 2] {};
        result = io.readHoldingRegisters(io.context, write.address, write.count, payload);
        if (!result) { return { ExecutionError::ReadbackFailed, result, i }; }

        for (uint8_t word = 0; word < write.count; ++word) {
            uint16_t observed = (static_cast<uint16_t>(payload[word * 2]) << 8)
                | payload[word * 2 + 1];
            if (observed != write.values[word]) {
                return { ExecutionError::VerificationFailed,
                    { Modbus::Error::InvalidData, 0, true }, i };
            }
        }
    }
    return {};
}

} // namespace Hems::Actuation
