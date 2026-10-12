// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/modbus/Protocol.h>
#include <cstring>

namespace Hems::Modbus {

char const* errorName(Error error)
{
    switch (error) {
        case Error::None: return "None";
        case Error::Network: return "Network";
        case Error::DnsFailure: return "DnsFailure";
        case Error::DnsTimeout: return "DnsTimeout";
        case Error::Connection: return "Connection";
        case Error::ConnectTimeout: return "ConnectTimeout";
        case Error::Timeout: return "Timeout";
        case Error::Protocol: return "Protocol";
        case Error::Exception: return "Exception";
        case Error::InvalidData: return "InvalidData";
        case Error::UnsupportedTable: return "UnsupportedTable";
        case Error::NoEvse: return "NoEvse";
        case Error::TaskUnavailable: return "TaskUnavailable";
    }
    return "Protocol";
}

bool makeReadRequest(uint8_t* frame, uint16_t transaction, uint8_t unit,
        ReadFunction function, uint16_t address, uint16_t count)
{
    if (count == 0 || count > MaxRegisters || static_cast<uint32_t>(address) + count > 65536
            || (function != ReadFunction::DiscreteInputs && function != ReadFunction::InputRegisters && function != ReadFunction::HoldingRegisters)) { return false; }
    uint8_t const request[] = {
        static_cast<uint8_t>(transaction >> 8), static_cast<uint8_t>(transaction),
        0, 0, 0, 6, unit, static_cast<uint8_t>(function),
        static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address),
        static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count)
    };
    memcpy(frame, request, sizeof(request));
    return true;
}

Result parseReadResponse(uint8_t const* frame, size_t size, uint16_t transaction,
        uint8_t unit, ReadFunction function, uint16_t count, uint8_t* payload)
{
    if (count == 0 || count > MaxRegisters || size < 9 || size > MaxFrame
            || (function != ReadFunction::DiscreteInputs && function != ReadFunction::InputRegisters && function != ReadFunction::HoldingRegisters)
            || frame[0] != (transaction >> 8) || frame[1] != (transaction & 0xff)
            || frame[2] != 0 || frame[3] != 0 || frame[4] != 0
            || frame[5] != size - 6 || frame[6] != unit) {
        return { Error::Protocol, 0, true };
    }
    auto code = static_cast<uint8_t>(function);
    if (frame[7] == (code | 0x80)) {
        if (size != 9 || frame[8] == 0) { return { Error::Protocol, 0, true }; }
        return { Error::Exception, frame[8], true };
    }
    size_t bytes = function != ReadFunction::DiscreteInputs ? count * 2 : (count + 7) / 8;
    if (frame[7] != code || frame[8] != bytes || size != bytes + 9) { return { Error::Protocol, 0, true }; }
    memcpy(payload, frame + 9, bytes);
    return { Error::None, 0, true };
}

uint32_t decodeUint32(uint8_t const* bytes)
{
    // WARP: network byte order, high 16-bit word first. See docs/HEMS_WARP.md.
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16)
        | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

float decodeFloat32(uint8_t const* bytes)
{
    static_assert(sizeof(float) == sizeof(uint32_t), "IEEE-754 float32 required");
    uint32_t bits = decodeUint32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace Hems::Modbus
