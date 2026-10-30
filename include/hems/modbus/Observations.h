// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/modbus/Protocol.h>
#include <cstddef>
namespace Hems::Modbus {
// Only data reads, never identity/credential/control registers. One successful
// provider cycle replaces this cache; telemetry never initiates a read.
struct Observations {
    struct Word { uint16_t address = 0, value = 0; ReadFunction function = ReadFunction::InputRegisters; };
    static constexpr size_t Capacity = 48;
    Word words[Capacity] {};
    uint16_t dropped = 0;
    uint8_t count = 0;
    void record(ReadFunction function, uint16_t address, uint16_t length, uint8_t const* bytes) {
        for (uint16_t i = 0; i < length; ++i) {
            if (count == Capacity) { ++dropped; continue; }
            uint16_t value = function == ReadFunction::DiscreteInputs ? ((bytes[i / 8] >> (i % 8)) & 1)
                : (uint16_t(bytes[2 * i]) << 8) | bytes[2 * i + 1];
            words[count++] = {static_cast<uint16_t>(address + i), value, function};
        }
    }

};
} // namespace Hems::Modbus
