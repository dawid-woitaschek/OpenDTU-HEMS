// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/warp/ProfileAdapter.h>
#include <cmath>

namespace Hems::Warp {
namespace {

bool unsignedValue(Evse::NumberValue const& value, uint32_t maximum, uint32_t& output)
{
    if (!value.valid || !std::isfinite(value.value) || value.value < 0
            || value.value > maximum || std::floor(value.value) != value.value) {
        return false;
    }
    output = static_cast<uint32_t>(value.value);
    return true;
}

} // namespace

bool fromProfileSample(Evse::Sample const& source, Sample& target)
{
    target = {};
    target.raw = source.raw;

    if (!unsignedValue(source.tableVersion, 5, target.tableVersion)
            || target.tableVersion < 2
            || !unsignedValue(source.iecState, 4, target.iecState)
            || !unsignedValue(source.chargingState, 4, target.chargingState)
            || !source.allowedCurrentA.valid
            || !std::isfinite(source.allowedCurrentA.value)
            || source.allowedCurrentA.value < 0
            || source.allowedCurrentA.value > 32
            || (source.allowedCurrentA.value != 0 && source.allowedCurrentA.value < 6)) {
        return false;
    }
    target.allowedCurrentA = static_cast<float>(source.allowedCurrentA.value);

    if (source.vehicleConnected.valid) {
        target.vehicleKnown = true;
        target.vehicleConnected = source.vehicleConnected.value;
    } else if (target.iecState < 4 && target.chargingState < 4) {
        target.vehicleKnown = true;
        target.vehicleConnected = target.iecState != 0;
    }

    if (source.charging.valid) {
        target.chargingKnown = true;
        target.charging = source.charging.value;
    } else if (target.iecState < 4 && target.chargingState < 4) {
        target.chargingKnown = true;
        target.charging = target.chargingState == 3;
    }

    target.meterAvailable = source.meterAvailable.valid
        ? source.meterAvailable.value
        : source.powerW.valid && source.sessionEnergyKWh.valid;
    if (target.meterAvailable) {
        if (!source.powerW.valid || !source.sessionEnergyKWh.valid
                || !std::isfinite(source.powerW.value) || source.powerW.value < 0
                || !std::isfinite(source.sessionEnergyKWh.value) || source.sessionEnergyKWh.value < 0) {
            return false;
        }
        target.powerW = static_cast<float>(source.powerW.value);
        target.sessionEnergyKWh = static_cast<float>(source.sessionEnergyKWh.value);
    }

    return true;
}

} // namespace Hems::Warp
