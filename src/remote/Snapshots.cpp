// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Snapshots.h>
#include <cmath>
namespace Remote {
namespace {
void rawJson(Hems::Modbus::Observations const& raw, JsonObject out) {
    out["dropped"] = raw.dropped;
    auto rows = out["registers"].to<JsonArray>();
    for (size_t i = 0; i < raw.count; ++i) {
        auto row = rows.add<JsonObject>(); row["function"] = static_cast<unsigned>(raw.words[i].function);
        row["address"] = raw.words[i].address; row["value"] = raw.words[i].value;
    }
}
template<class Snapshot> void connection(Snapshot const& s, JsonObject out) {
    out["configured"] = s.configured; out["enabled"] = s.enabled; out["reachable"] = s.reachable;
    out["valid"] = s.valid; out["stale"] = s.stale; out["has_update"] = s.hasUpdate;
    out["age_ms"] = nullptr; if (s.hasUpdate) { out["age_ms"] = s.ageMs; }
    out["last_success_ms"] = s.lastSuccessMs;
    out["successful_polls"] = s.successfulPolls; out["failed_polls"] = s.failedPolls;
    out["error"] = Hems::Modbus::errorName(s.error); out["modbus_exception"] = s.exception;
}
}
void storageJson(Hems::Storage::Snapshot const& s, JsonObject out) {
    connection(s, out); out["provider"] = "modbus_profile"; out["profile_loaded"] = s.profileLoaded;
    auto native = out["last_observations"].to<JsonObject>();
    // Last observed values retain their timestamp/validity even when stale.
    auto number = [&](char const* key, Hems::Storage::NumberValue const& n) { native[key] = nullptr; if (n.valid) { native[key] = n.value; } };
    auto flag = [&](char const* key, Hems::Storage::BoolValue const& n) { native[key] = nullptr; if (n.valid) { native[key] = n.value; } };
    auto const& v = s.sample;
    // Collector summary aliases describe current valid observations; detailed
    // last_observations below still retain their original validity/age semantics.
    if (s.valid && v.socPct.valid && std::isfinite(v.socPct.value)) { out["state_of_charge"] = v.socPct.value; }
    number("soc_pct", v.socPct); number("ac_power_w", v.acPowerW); number("dc_power_w", v.dcPowerW);
    number("battery_voltage_v", v.batteryVoltageV); number("battery_current_a", v.batteryCurrentA);
    number("temperature_c", v.temperatureC); number("charge_energy_total_kwh", v.chargeEnergyTotalKWh);
    number("discharge_energy_total_kwh", v.dischargeEnergyTotalKWh); number("operating_state", v.operatingState);
    number("fault_code", v.faultCode); number("alarm_code", v.alarmCode);
    flag("charging", v.charging); flag("discharging", v.discharging); flag("fault_active", v.faultActive); flag("alarm_active", v.alarmActive);
    rawJson(s.sample.raw, out["raw_modbus"].to<JsonObject>());
}
void wallboxJson(Hems::Warp::Snapshot const& s, JsonObject out) {
    connection(s, out); out["provider"] = "warp_modbus"; out["profile_loaded"] = s.profileLoaded;
    out["connected"] = s.reachable; // Provider reachability, not vehicle presence.
    out["profile_driven"] = s.profileDriven;
    auto native = out["last_observations"].to<JsonObject>();
    auto const& v = s.sample;
    if (s.valid && s.hasUpdate && v.chargingKnown) { out["charging"] = v.charging; }
    for (auto key : {"table_version", "iec_state", "charging_state", "allowed_current_a", "power_w", "session_energy_kwh", "vehicle_connected", "charging"}) { native[key] = nullptr; }
    if (s.hasUpdate) {
        native["table_version"] = v.tableVersion; native["iec_state"] = v.iecState;
        native["charging_state"] = v.chargingState; native["allowed_current_a"] = v.allowedCurrentA;
        native["meter_available"] = v.meterAvailable;
        if (v.meterAvailable) { native["power_w"] = v.powerW; native["session_energy_kwh"] = v.sessionEnergyKWh; }
        if (v.vehicleKnown) { native["vehicle_connected"] = v.vehicleConnected; }
        if (v.chargingKnown) { native["charging"] = v.charging; }
    }
    rawJson(s.sample.raw, out["raw_modbus"].to<JsonObject>());
}
} // namespace Remote
