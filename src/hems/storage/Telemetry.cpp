// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Telemetry.h>
#include <cstring>

namespace Hems::Storage {
namespace {

NumberValue* field(Sample& sample, char const* key)
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

} // namespace

bool supportedKey(char const* key)
{
    if (!key) { return false; }
    Sample sample;
    return field(sample, key) != nullptr;
}

bool apply(Sample& sample, char const* key, Profiles::Value const& value)
{
    auto* target = key ? field(sample, key) : nullptr;
    if (!target) { return false; }
    ++sample.recognizedValues;
    // v1 storage semantics are numeric. A mismatched bool remains explicit unknown.
    if (!value.valid || value.type == Profiles::Type::Bool) { return true; }
    target->valid = true;
    target->value = value.number;
    ++sample.validValues;
    return true;
}

} // namespace Hems::Storage
