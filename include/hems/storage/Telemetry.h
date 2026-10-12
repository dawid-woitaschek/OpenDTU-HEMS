// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/profiles/Profile.h>

namespace Hems::Storage {

struct NumberValue {
    bool valid = false;
    double value = 0;
};

struct Sample {
    NumberValue socPct;
    NumberValue acPowerW;
    NumberValue dcPowerW;
    NumberValue batteryVoltageV;
    NumberValue batteryCurrentA;
    NumberValue temperatureC;
    NumberValue chargeEnergyTotalKWh;
    NumberValue dischargeEnergyTotalKWh;
    NumberValue operatingState;
    NumberValue faultCode;
    NumberValue alarmCode;
    uint8_t recognizedValues = 0;
    uint8_t validValues = 0;
};

bool supportedKey(char const* key);
bool apply(Sample& sample, char const* key, Profiles::Value const& value);

} // namespace Hems::Storage
