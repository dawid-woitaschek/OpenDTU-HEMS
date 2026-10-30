// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/evse/Telemetry.h>

namespace Hems::Evse {

// Deterministic read-only seam. Reader deliberately exposes only FC2/FC3/FC4.
// Unknown profile keys are ignored so vendor-specific diagnostics do not make a
// profile unusable for the generic EVSE consumer.
template<typename Reader>
Modbus::Result poll(Reader& reader, Profiles::Profile const& profile, Sample& sample)
{
    sample = {};
    for (size_t i = 0; i < profile.entryCount; ++i) {
        auto const& entry = profile.entries[i];
        if (!supportedKey(entry.key)) { continue; }

        uint8_t payload[4] {};
        Modbus::Result result;
        switch (entry.space) {
            case Modbus::ReadFunction::DiscreteInputs:
                result = reader.readDiscreteInputs(entry.address, entry.count, payload);
                break;
            case Modbus::ReadFunction::HoldingRegisters:
                result = reader.readHoldingRegisters(entry.address, entry.count, payload);
                break;
            case Modbus::ReadFunction::InputRegisters:
                result = reader.readInputRegisters(entry.address, entry.count, payload);
                break;
        }
        if (!result) { return result; }

        sample.raw.record(entry.space, entry.address, entry.count, payload);
        size_t payloadSize = entry.type == Profiles::Type::Bool ? 1 : entry.count * 2;
        apply(sample, entry.key, Profiles::decode(entry, payload, payloadSize));
    }

    if (sample.recognizedValues == 0 || sample.validValues == 0) {
        return { Modbus::Error::InvalidData, 0, true };
    }
    return { Modbus::Error::None, 0, true };
}

} // namespace Hems::Evse
