// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/HemsSnapshot.h>
namespace Remote {
namespace {
void device(Hems::Model::DeviceState const& state, JsonObject out) {
    out["configured"] = state.configured; out["enabled"] = state.enabled; out["reachable"] = state.reachable;
    out["has_update"] = state.hasUpdate; out["age_ms"] = nullptr;
    if (state.hasUpdate) { out["age_ms"] = state.ageMs; }
}
}
void normalizedJson(Hems::Model::SystemState const& s, JsonObject out) {
    signalJson(s.grid.powerW, out["grid"]["power_w"].to<JsonObject>());
    signalJson(s.pv.powerW, out["pv"]["ac_power_w"].to<JsonObject>());
    auto storage = out["storage"].to<JsonObject>(); device(s.storage.device, storage["device"].to<JsonObject>());
    signalJson(s.storage.socPct, storage["soc_pct"].to<JsonObject>());
    signalJson(s.storage.acPowerW, storage["ac_power_w"].to<JsonObject>());
    signalJson(s.storage.dcPowerW, storage["dc_power_w"].to<JsonObject>());
    signalJson(s.storage.batteryVoltageV, storage["battery_voltage_v"].to<JsonObject>());
    signalJson(s.storage.batteryCurrentA, storage["battery_current_a"].to<JsonObject>());
    signalJson(s.storage.temperatureC, storage["temperature_c"].to<JsonObject>());
    signalJson(s.storage.charging, storage["charging"].to<JsonObject>());
    signalJson(s.storage.discharging, storage["discharging"].to<JsonObject>());
    signalJson(s.storage.faultActive, storage["fault"].to<JsonObject>());
    signalJson(s.storage.alarmActive, storage["alarm"].to<JsonObject>());
    auto evse = out["evse"].to<JsonObject>(); device(s.evse.device, evse["device"].to<JsonObject>());
    signalJson(s.evse.powerW, evse["power_w"].to<JsonObject>());
    signalJson(s.evse.vehicleConnected, evse["vehicle_connected"].to<JsonObject>());
    signalJson(s.evse.charging, evse["charging"].to<JsonObject>());
    signalJson(s.evse.allowedCurrentA, evse["allowed_current_a"].to<JsonObject>());
}
void constraintsJson(Hems::Constraints::StorageConstraints const& c, JsonObject out) {
    out["charge_allowed"] = c.chargeAllowed; out["discharge_allowed"] = c.dischargeAllowed;
    out["max_charge_power_w"] = nullptr; out["max_discharge_power_w"] = nullptr;
    if (c.maxChargePower.active) { out["max_charge_power_w"] = c.maxChargePower.valueW; }
    if (c.maxDischargePower.active) { out["max_discharge_power_w"] = c.maxDischargePower.valueW; }
}
void readinessJson(Hems::Actuation::Readiness r, JsonObject out) {
    using Hems::Actuation::ReadinessBlocker;
    using Hems::Actuation::readinessBlockerName;
    out["writes_possible"] = r.allowed(); out["blocker_mask"] = r.blockers;
    auto rows = out["blockers"].to<JsonArray>();
    for (auto reason : {ReadinessBlocker::GlobalModeInactive, ReadinessBlocker::DevicePermissionMissing,
            ReadinessBlocker::DeviceNotConfigured, ReadinessBlocker::DeviceNotEnabled, ReadinessBlocker::ProfileNotControllable,
            ReadinessBlocker::DeviceUnreachable, ReadinessBlocker::TelemetryNotFresh, ReadinessBlocker::CommandBusy}) {
        if (r.has(reason)) { rows.add(readinessBlockerName(reason)); }
    }
}
} // namespace Remote
