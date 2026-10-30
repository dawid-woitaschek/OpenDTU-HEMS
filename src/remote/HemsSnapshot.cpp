// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/HemsSnapshot.h>
#include <hems/Controller.h>
#include <hems/energy/Service.h>
#include <hems/optimizer/Service.h>
#include <Arduino.h>
namespace Remote {
using Hems::ControlTarget;
using Hems::controlModeName;
using Hems::modeName;
namespace Energy = Hems::Energy;
namespace Optimizer = Hems::Optimizer;
namespace Actuation = Hems::Actuation;
namespace Policies = Hems::Policies;
namespace Flow = Hems::Flow;
uint32_t hemsPlanRevision() { return Hems::Optimizer::ShadowOptimizer.publishedRevision(); }
bool hemsSnapshot(JsonObject out) {
    out["read_only"] = true; out["global_mode"] = modeName(HemsController.getMode());
    for (auto target : {ControlTarget::Storage, ControlTarget::Evse}) {
        auto control = out["control"][target == ControlTarget::Storage ? "storage" : "evse"].to<JsonObject>();
        control["mode"] = controlModeName(HemsController.getControlMode(target));
        control["permission"] = HemsController.controlPermissionGranted(target);
        readinessJson(HemsController.getActuationReadiness(target), control["manual"].to<JsonObject>());
        readinessJson(HemsController.automaticReadiness(target), control["automatic"].to<JsonObject>());
    }
    normalizedJson(HemsController.getNormalizedState(), out["normalized"].to<JsonObject>());
    if (!Energy::EnergyData.serializeRemote(Energy::Domain::Tariff, out["tariff"].to<JsonObject>())
        || !Energy::EnergyData.serializeRemote(Energy::Domain::Forecast, out["forecast"].to<JsonObject>())
        || !Optimizer::ShadowOptimizer.serializeRemote(out["optimizer"].to<JsonObject>())) { return false; }
    HemsController.serializeEconomic(out["economic_dispatch"].to<JsonObject>());
    auto preview = HemsController.shadowIfActive(); auto shadow = out["shadow_if_active"].to<JsonObject>();
    shadow["valid"] = preview.valid; shadow["action"] = nullptr; shadow["power_w"] = nullptr;
    shadow["reason"] = preview.reason; shadow["plan_revision"] = preview.planRevision;
    shadow["assumed_mode"] = "Active"; shadow["assumed_storage_permission"] = true;
    shadow["device_writes"] = false;
    if (preview.valid) {
        shadow["action"] = Actuation::actionName(preview.command.action);
        auto value = preview.command.hasValue ? preview.command.value : 0;
        shadow["power_w"] = preview.command.action == Actuation::Action::SetDischargePowerW ? -value : value;
    }
    auto constraints = out["constraints"].to<JsonObject>();
    auto physical = HemsController.getStoragePowerLimits();
    constraints["physical"]["known"] = physical.known;
    constraints["physical"]["max_charge_w"] = nullptr; constraints["physical"]["max_discharge_w"] = nullptr;
    if (physical.known) { constraints["physical"]["max_charge_w"] = physical.chargeW; constraints["physical"]["max_discharge_w"] = physical.dischargeW; }
    constraintsJson(HemsController.getConstraints().storage, constraints["applicable"].to<JsonObject>());
    auto protection = HemsController.getEvProtectionShadowSnapshot();
    auto ev = constraints["ev_protection"].to<JsonObject>();
    ev["state"] = Policies::evProtectionStateName(protection.state); ev["condition_age_ms"] = protection.conditionAgeMs;
    ev["charging"] = nullptr; if (protection.chargingKnown) { ev["charging"] = protection.charging; }
    constraintsJson(protection.proposedConstraints.storage, ev["proposed"].to<JsonObject>());
    constraintsJson(protection.actionableConstraints.storage, ev["actionable"].to<JsonObject>());
    auto dpl = Flow::Coordination.dpl(); auto envelope = Flow::Coordination.envelope();
    auto flow = out["flow"].to<JsonObject>(); bool fresh = dpl.atMs && millis() - dpl.atMs <= 3000;
    flow["dpl_available"] = fresh && dpl.available; flow["dpl_fresh"] = fresh;
    flow["topology_supported"] = fresh && dpl.topologySupported; flow["age_ms"] = nullptr;
    flow["pv_headroom"] = nullptr; flow["physical_pv_ac_w"] = nullptr; flow["solar_dormant"] = nullptr;
    if (dpl.atMs) { flow["age_ms"] = millis() - dpl.atMs; }
    if (fresh) { flow["pv_headroom"] = dpl.pvHeadroom; flow["physical_pv_ac_w"] = dpl.physicalAcW; flow["solar_dormant"] = dpl.solarDormant; }
    flow["envelope_enabled"] = envelope.enabled; flow["envelope_fresh"] = envelope.atMs && millis() - envelope.atMs <= 3000;
    flow["pv_limit_w"] = envelope.pvLimitW;
    flow["observations"] = "normalized.grid, normalized.pv, normalized.storage";
    return true;
}
} // namespace Remote
