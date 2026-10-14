// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/evse/Telemetry.h>
#include <cstring>

namespace Hems::Evse {
namespace {

NumberValue* numberField(Sample& sample, char const* key)
{
    if (!strcmp(key, "register_table_version")) { return &sample.tableVersion; }
    if (!strcmp(key, "iec_state")) { return &sample.iecState; }
    if (!strcmp(key, "charging_state")) { return &sample.chargingState; }
    if (!strcmp(key, "allowed_current_a")) { return &sample.allowedCurrentA; }
    if (!strcmp(key, "power_w")) { return &sample.powerW; }
    if (!strcmp(key, "session_energy_kwh")) { return &sample.sessionEnergyKWh; }
    return nullptr;
}

BoolValue* boolField(Sample& sample, char const* key)
{
    if (!strcmp(key, "vehicle_connected")) { return &sample.vehicleConnected; }
    if (!strcmp(key, "charging")) { return &sample.charging; }
    if (!strcmp(key, "meter_available")) { return &sample.meterAvailable; }
    return nullptr;
}

} // namespace

bool supportedKey(char const* key)
{
    if (!key) { return false; }
    Sample sample;
    return numberField(sample, key) != nullptr || boolField(sample, key) != nullptr;
}

bool supportsProfile(Profiles::Profile const& profile)
{
    if (!Profiles::hasCapability(profile, Profiles::Capability::Evse)) { return false; }
    for (uint8_t i = 0; i < profile.entryCount; ++i) {
        if (supportedKey(profile.entries[i].key)) { return true; }
    }
    return false;
}

bool apply(Sample& sample, char const* key, Profiles::Value const& value)
{
    if (!key) { return false; }
    if (auto* target = numberField(sample, key)) {
        ++sample.recognizedValues;
        if (!value.valid || value.type == Profiles::Type::Bool) { return true; }
        target->valid = true;
        target->value = value.number;
        ++sample.validValues;
        return true;
    }
    if (auto* target = boolField(sample, key)) {
        ++sample.recognizedValues;
        if (!value.valid) { return true; }
        if (value.type == Profiles::Type::Bool) {
            target->valid = true;
            target->value = value.boolean;
        } else if (value.number == 0 || value.number == 1) {
            target->valid = true;
            target->value = value.number == 1;
        } else {
            return true;
        }
        ++sample.validValues;
        return true;
    }
    return false;
}

} // namespace Hems::Evse
