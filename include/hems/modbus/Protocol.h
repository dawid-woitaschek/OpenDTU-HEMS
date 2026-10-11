// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace Hems::Modbus {

enum class Error : uint8_t {
    None, Network, DnsFailure, DnsTimeout, Connection, ConnectTimeout,
    Timeout, Protocol, Exception, InvalidData, UnsupportedTable, NoEvse, TaskUnavailable
};

char const* errorName(Error error);

// The public protocol surface can only construct read requests.
enum class ReadFunction : uint8_t { DiscreteInputs = 2, InputRegisters = 4 };
constexpr size_t MaxRegisters = 16;
constexpr size_t MaxFrame = 9 + 2 * MaxRegisters;

struct Result {
    Error error = Error::None;
    uint8_t exception = 0;
    bool responded = false;
    explicit operator bool() const { return error == Error::None; }
};

bool makeReadRequest(uint8_t* frame, uint16_t transaction, uint8_t unit,
        ReadFunction function, uint16_t address, uint16_t count);
Result parseReadResponse(uint8_t const* frame, size_t size, uint16_t transaction,
        uint8_t unit, ReadFunction function, uint16_t count, uint8_t* payload);
uint32_t decodeUint32(uint8_t const* bytes);
float decodeFloat32(uint8_t const* bytes);

} // namespace Hems::Modbus
