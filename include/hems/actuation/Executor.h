// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/modbus/Protocol.h>
#include <hems/profiles/ControlPlan.h>

namespace Hems::Actuation {

struct RegisterIo {
    void* context = nullptr;
    Modbus::Result (*writeHoldingRegisters)(void* context, uint16_t address,
            uint16_t count, uint16_t const* values) = nullptr;
    Modbus::Result (*readHoldingRegisters)(void* context, uint16_t address,
            uint16_t count, uint8_t* payload) = nullptr;
    // Optional last-moment gate. When supplied it is evaluated immediately
    // before every write, so a command cannot continue after authorization was
    // revoked while it was waiting or between bounded profile writes.
    bool (*authorized)(void* context) = nullptr;
};

enum class ExecutionError : uint8_t {
    None,
    InvalidIo,
    AuthorizationRevoked,
    WriteFailed,
    ReadbackFailed,
    VerificationFailed,
};

char const* executionErrorName(ExecutionError error);

struct ExecutionResult {
    ExecutionError error = ExecutionError::None;
    Modbus::Result transport {};
    uint8_t writeIndex = 0xff;
    // A failed response/readback does not prove that the device rejected a write.
    bool writeAttempted = false;
    explicit operator bool() const { return error == ExecutionError::None; }
};

ExecutionResult execute(RegisterIo const& io, Profiles::ControlPlan const& plan,
        bool verifyReadback = true);

} // namespace Hems::Actuation
