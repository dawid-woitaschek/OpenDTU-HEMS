// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/warp/State.h>

namespace Hems::Warp {

// Small deterministic seam: Reader has only input-register/discrete-input reads.
// Never read optional meter registers on Smart models or unavailable meters.
template<typename Reader>
Modbus::Result poll(Reader& reader, Sample& sample)
{
    uint8_t version[4] {};
    auto result = reader.readInputRegisters(0, 2, version);
    if (!result) { return result; }
    sample.tableVersion = Modbus::decodeUint32(version);
    if (sample.tableVersion < 2 || sample.tableVersion > 5) {
        return { Modbus::Error::UnsupportedTable, 0, true };
    }
    uint8_t features[1] {};
    result = reader.readDiscreteInputs(0, 2, features);
    if (!result) { return result; }
    if (!(features[0] & 1)) { return { Modbus::Error::NoEvse, 0, true }; }
    sample.meterAvailable = features[0] & 2;
    uint8_t evse[24] {};
    result = reader.readInputRegisters(1000, 12, evse);
    if (!result) { return result; }
    uint8_t meter[16] {};
    if (sample.meterAvailable) {
        result = reader.readInputRegisters(2002, 8, meter);
        if (!result) { return result; }
    }
    if (!decode(sample, evse, sample.meterAvailable ? meter : nullptr)) {
        return { Modbus::Error::InvalidData, 0, true };
    }
    return { Modbus::Error::None, 0, true };
}

} // namespace Hems::Warp
