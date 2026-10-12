// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Telemetry.h>
#include <cstring>

namespace Hems::Storage {
namespace {

NumberValue* numberField(Sample& sample, char const* key)
{
    if (!strcmp(key, "soc_pct")) { return &sample.socPct; }
    if (!strcmp(key, "ac_power_w")) { return &sample.acPowerW; }
    if (!strcmp(key, "dc_power_w")) { return &sample.dcPowerW; }
    if (!strcmp(key, "battery_voltage_v")) { return &sample.batteryVoltageV; }
    if (!strcmp(key, "battery_current_a")) { return &sample.batteryCurrentA; }
    if (!strcmp(key, "temperature_c")) { return &sample.temperatureC; }
    if (!strcmp(key, "charge_energy_total_kwh")) { return &sample.chargeEnergyTotalKWh; }
    if (!strcmp(key, "discharge_energy_total_kwh")) { return &sample.dischargeEnergyTotalKWh; }
    if (!strcmp(key, "operating_state")) { return &sample.operatingState; }
    if (!strcmp(key, "fault_code")) { return &sample.faultCode; }
    if (!strcmp(key, "alarm_code")) { return &sample.alarmCode; }
    return nullptr;
}

BoolValue* boolField(Sample& sample, char const* key)
{
    if (!strcmp(key, "charging")) { return &sample.charging; }
    if (!strcmp(key, "discharging")) { return &sample.discharging; }
    if (!strcmp(key, "fault_active")) { return &sample.faultActive; }
    if (!strcmp(key, "alarm_active")) { return &sample.alarmActive; }
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
    // Semantic overlap alone is not a device identity. A wallbox may expose
    // "charging", temperature or fault codes too. Require an explicit consumer
    // capability and at least one semantic the storage provider can normalize.
    if (!Profiles::hasCapability(profile, Profiles::Capability::Storage)) { return false; }
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
            // Many devices expose flags as 0/1 holding/input registers rather
            // than coils/discrete inputs. Accept that generic representation,
            // but reject arbitrary non-zero enums as booleans.
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

} // namespace Hems::Storage
