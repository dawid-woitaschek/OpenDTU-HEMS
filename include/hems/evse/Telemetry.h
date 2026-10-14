// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/profiles/Profile.h>

namespace Hems::Evse {

struct NumberValue {
    bool valid = false;
    double value = 0;
};

struct BoolValue {
    bool valid = false;
    bool value = false;
};

struct Sample {
    NumberValue tableVersion;
    NumberValue iecState;
    NumberValue chargingState;
    NumberValue allowedCurrentA;
    NumberValue powerW;
    NumberValue sessionEnergyKWh;
    BoolValue vehicleConnected;
    BoolValue charging;
    BoolValue meterAvailable;
    uint8_t recognizedValues = 0;
    uint8_t validValues = 0;
};

bool supportedKey(char const* key);
bool supportsProfile(Profiles::Profile const& profile);
bool apply(Sample& sample, char const* key, Profiles::Value const& value);

} // namespace Hems::Evse
