// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Planner.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace Hems::Optimizer {
namespace {
bool number(JsonVariantConst j, char const* key, uint32_t lo, uint32_t hi) {
    auto v = j[key]; return v.is<uint32_t>() && v.as<uint32_t>() >= lo && v.as<uint32_t>() <= hi;
}
constexpr int64_t Infinity = std::numeric_limits<int64_t>::max() / 4;
// DP objective is in half-nano currency to retain the half-throughput convention.
struct Score { int64_t cost = Infinity; uint64_t throughput = UINT64_MAX; };
bool better(Score a, Score b) { return a.cost < b.cost || (a.cost == b.cost && a.throughput < b.throughput); }
struct Workspace {
    Score previous[MaxStates], next[MaxStates];
    union { uint8_t parent[MaxSteps][MaxStates] {}; int32_t priceScratch[MaxSteps]; };
    uint32_t energy[MaxStates] {};
    int64_t terminal[MaxStates] {};
};
// nano currency = price micro currency/kWh * Ws / 3600. Split before multiplication.
int64_t money(int64_t ws, int32_t price) { return (ws / 3600) * price + ((ws % 3600) * price) / 3600; }
struct Flow {
    int64_t batteryWs = 0, importWs = 0, exportWs = 0, cost = 0;
    int64_t importCost = 0, exportRevenue = 0, degradation = 0, buffer = 0, curtailedWs = 0;
};
int64_t terminalValue(Config const& c, Plan const& p, uint32_t wh) {
    uint32_t floor = static_cast<uint64_t>(c.capacityWh) * c.minSoc / 100;
    // Avoided AC imports, bounded by explicit continuation demand. No export
    // or perfect future forecast is assumed beyond the covered horizon.
    int64_t acWs = static_cast<int64_t>(std::min(wh > floor ? wh - floor : 0, p.terminalDemandWh)) * 3600 * c.dischargePermille / 1000;
    return -money(acWs, p.terminalPrice);
}
void applyFlow(Step& slot, Flow const& f) {
    slot.batteryW = f.batteryWs / (slot.end - slot.start);
    slot.gridImportWs = f.importWs; slot.gridExportWs = f.exportWs; slot.costNano = f.cost / 2;
}
void totals(Config const& c, Plan& p) {
    p.importNano = p.exportNano = p.degradationNano = p.bufferNano = p.curtailedWs = 0; p.internalAbsWh = 0;
    for (size_t n = 0; n < p.count; ++n) {
        auto const& s = p.steps[n];
        p.importNano += money(s.gridImportWs, s.usedPrice);
        p.exportNano += c.exportConfigured ? money(s.gridExportWs, c.exportMicros) : 0;
        int64_t deltaWs = (static_cast<int64_t>(s.endWh) - s.startWh) * 3600;
        int64_t batteryWs = deltaWs > 0 ? (deltaWs * 1000 + c.chargePermille - 1) / c.chargePermille : deltaWs * c.dischargePermille / 1000;
        p.curtailedWs += std::max<int64_t>(0, s.gridImportWs - s.gridExportWs - (static_cast<int64_t>(s.loadW) - s.pvW) * (s.end - s.start) - batteryWs);
        int64_t absWh = std::abs(static_cast<int64_t>(s.endWh) - s.startWh);
        p.internalAbsWh += absWh;
    }
    // Round only after summing internal reference throughput across the plan.
    p.degradationNano = static_cast<int64_t>(p.internalAbsWh) * c.degradationMicros / 2;
    p.bufferNano = static_cast<int64_t>(p.internalAbsWh) * c.bufferMicros / 2;
}
bool transition(Config const& c, Policies::StorageEconomicGoals const& goals, Inputs const& in, Step const& slot, size_t index, uint32_t from, uint32_t to, Flow& flow)
{
    uint32_t seconds = slot.end - slot.start;
    int64_t deltaWs = (static_cast<int64_t>(to) - from) * 3600;
    int64_t netWs = (static_cast<int64_t>(slot.loadW) - slot.pvW) * seconds;
    uint32_t chargeW = c.maxChargeW, dischargeW = c.maxDischargeW;
    if (in.storageLimits.known) {
        chargeW = std::min(chargeW, in.storageLimits.chargeW);
        dischargeW = std::min(dischargeW, in.storageLimits.dischargeW);
    }
    if (!index) {
        auto const& constraints = in.currentConstraints;
        if (!constraints.chargeAllowed) { chargeW = 0; }
        if (!constraints.dischargeAllowed) { dischargeW = 0; }
        if (constraints.maxChargePower.active) { chargeW = std::min(static_cast<double>(chargeW), std::max(0.0, constraints.maxChargePower.valueW)); }
        if (constraints.maxDischargePower.active) { dischargeW = std::min(static_cast<double>(dischargeW), std::max(0.0, constraints.maxDischargePower.valueW)); }
    }
    if (c.flow.evProtection && in.evRestrictions) {
        auto ev = in.evRestrictions->current(slot.start);
        if (ev && ev->value > 0) { dischargeW = 0; }
    }
    if (deltaWs < 0 && !goals.dischargeAllowed) { return false; }
    if (deltaWs > 0) {
        flow.batteryWs = (deltaWs * 1000 + c.chargePermille - 1) / c.chargePermille;
        // Grid charging means intentional additional import for the battery, after PV covers load.
        if (!goals.gridChargingAllowed && flow.batteryWs > std::max(static_cast<int64_t>(0), -netWs)) { return false; }
        if (flow.batteryWs > static_cast<int64_t>(chargeW) * seconds) { return false; }
    } else if (deltaWs < 0) {
        flow.batteryWs = -((-deltaWs * c.dischargePermille) / 1000);
        if (-flow.batteryWs > static_cast<int64_t>(dischargeW) * seconds) { return false; }
        // Without explicit compensation, storage never discharges solely for export.
        if (!c.flow.batteryExport && -flow.batteryWs > std::max(static_cast<int64_t>(0), netWs)) { return false; }
    }
    // Export-first reserves the permitted surplus for export before charging.
    if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::ExportFirst && c.flow.pvExport
            && deltaWs > 0 && !goals.gridChargingAllowed
            && flow.batteryWs > std::max<int64_t>(0, -netWs - static_cast<int64_t>(c.flow.maxExportW) * seconds)) { return false; }
    int64_t grid = netWs + flow.batteryWs;
    if (c.flow.enabled) {
        int64_t permittedExport = static_cast<int64_t>(c.flow.maxExportW) * seconds;
        // No battery export can be corrected by curtailing PV after PV reaches zero.
        if (flow.batteryWs < -static_cast<int64_t>(slot.loadW) * seconds - permittedExport) { return false; }
        if (!c.flow.pvExport) { permittedExport = c.flow.batteryExport && deltaWs < 0 ? permittedExport : 0; }
        flow.curtailedWs = std::min<int64_t>(static_cast<int64_t>(slot.pvW) * seconds, std::max<int64_t>(0, -grid - permittedExport));
        grid += flow.curtailedWs;
        if (grid < -permittedExport) { return false; }
        if (c.flow.preference == Hems::Flow::Preference::LocalFirst && deltaWs >= 0 && grid < 0 && to < goals.ceilingWh) {
            // Quantized DP: require every representable eligible PV charge before
            // curtailment. Less than one lattice increment remains feasible.
            int64_t quantumWs = static_cast<int64_t>(std::max<uint32_t>(1, (static_cast<uint64_t>(c.capacityWh) * c.socStepPct + 99) / 100)) * 3600 * 1000 / c.chargePermille;
            int64_t extra = std::min({-grid, static_cast<int64_t>(chargeW) * seconds - std::max<int64_t>(0, flow.batteryWs), static_cast<int64_t>(goals.ceilingWh - to) * 3600 * 1000 / c.chargePermille});
            if (extra >= quantumWs) { return false; }
        }
    }
    flow.importWs = std::max(static_cast<int64_t>(0), grid); flow.exportWs = std::max(static_cast<int64_t>(0), -grid);
    flow.importCost = money(flow.importWs, slot.usedPrice);
    flow.exportRevenue = c.exportConfigured ? money(flow.exportWs, c.exportMicros) : 0;
    // EFC-reference kWh = |internal delta Wh| / 2000. Preserve half-Wh
    // precision until currency nano conversion; efficiencies affect AC only.
    int64_t absWh = std::abs(static_cast<int64_t>(to) - from);
    flow.degradation = absWh * c.degradationMicros;
    flow.buffer = absWh * c.bufferMicros;
    flow.cost = 2 * (flow.importCost - flow.exportRevenue) + flow.degradation + flow.buffer;
    return true;
}
} // namespace
bool parseConfig(JsonVariantConst j, Config& out)
{
    if (!j["soc_bounds_configured"].is<bool>() || !j["enabled"].is<bool>() || !j["effective_price"].is<bool>() || !j["export_configured"].is<bool>()
            || !j["export_micros_per_kwh"].is<int32_t>()
            || !number(j, "horizon_seconds", 900, 172800) || !number(j, "capacity_wh", 0, 1000000)
            || !number(j, "min_soc_pct", 0, 99) || !number(j, "max_soc_pct", 1, 100)
            || !number(j, "charge_permille", 0, 1000) || !number(j, "discharge_permille", 0, 1000)
            || !number(j, "max_charge_w", 0, 100000) || !number(j, "max_discharge_w", 0, 100000)
            || !number(j, "expected_base_load_w", 0, 100000) || !number(j, "soc_step_pct", 2, 10)) { return false; }
    Config c; c.socBoundsConfigured = j["soc_bounds_configured"]; c.enabled = j["enabled"]; c.effectivePrice = j["effective_price"]; c.exportConfigured = j["export_configured"];
    if (!j["dispatch_enabled"].isUnbound()) {
        if (!j["dispatch_enabled"].is<bool>()) { return false; }
        c.dispatchEnabled = j["dispatch_enabled"];
    }
    c.exportMicros = j["export_micros_per_kwh"]; c.horizonSeconds = j["horizon_seconds"]; c.capacityWh = j["capacity_wh"];
    c.minSoc = j["min_soc_pct"]; c.maxSoc = j["max_soc_pct"]; c.chargePermille = j["charge_permille"];
    c.dischargePermille = j["discharge_permille"]; c.maxChargeW = j["max_charge_w"]; c.maxDischargeW = j["max_discharge_w"];
    c.baseLoadW = j["expected_base_load_w"]; c.socStepPct = j["soc_step_pct"];
    if (c.minSoc >= c.maxSoc || (c.chargePermille && c.chargePermille < 100) || (c.dischargePermille && c.dischargePermille < 100)) { return false; }
    // Additive v1 config: absent policies preserve legacy planning behavior; a present block is complete and typed.
    if (!j["policies"].isUnbound()) {
        auto p = j["policies"];
        if (!p.is<JsonObjectConst>() || !p["grid_charging_allowed"].is<bool>() || !p["discharge_allowed"].is<bool>()
                || !p["target_enabled"].is<bool>() || !number(p, "target_soc_pct", 0, 100)
                || !number(p, "target_deadline", 0, UINT32_MAX)) { return false; }
        c.policies.gridChargingAllowed = p["grid_charging_allowed"]; c.policies.dischargeAllowed = p["discharge_allowed"];
        c.policies.targetEnabled = p["target_enabled"]; c.policies.targetSoc = p["target_soc_pct"]; c.policies.targetDeadline = p["target_deadline"];
        if (c.policies.targetEnabled && (c.policies.targetSoc < c.minSoc || c.policies.targetSoc > c.maxSoc
                || c.policies.targetDeadline < Energy::MinUtc)) { return false; }
    }
    if (!j["energy_management"].isUnbound()) {
        auto p = j["energy_management"];
        for (auto key : {"enabled", "pv_export", "battery_export", "ev_protection", "automatic_load", "terminal_price_configured"}) {
            if (!p[key].is<bool>()) { return false; }
        }
        if (!number(p, "preference", 0, 2) || !number(p, "max_export_w", 0, 100000)
                || !number(p, "pv_ac_limit_w", 0, 100000) || !number(p, "wear_source", 0, 2)
                || !number(p, "degradation_micros", 0, 10000000) || !number(p, "buffer_micros", 0, 10000000)
                || !number(p, "efc_capacity_wh", 0, 1000000) || !number(p, "terminal_hours", 1, 48)
                || !number(p, "terminal_price_micros", 0, 10000000) || !number(p, "hysteresis_micros", 0, 10000000)) { return false; }
        c.flow.enabled = p["enabled"]; c.flow.pvExport = p["pv_export"]; c.flow.batteryExport = p["battery_export"];
        c.flow.evProtection = p["ev_protection"]; c.flow.preference = static_cast<Hems::Flow::Preference>(p["preference"].as<unsigned>());
        c.flow.maxExportW = p["max_export_w"]; c.flow.pvAcLimitW = p["pv_ac_limit_w"];
        c.automaticLoad = p["automatic_load"]; c.wearSource = p["wear_source"]; c.degradationMicros = p["degradation_micros"];
        c.bufferMicros = p["buffer_micros"]; c.efcCapacityWh = p["efc_capacity_wh"];
        c.terminalHours = p["terminal_hours"]; c.terminalPriceMicros = p["terminal_price_micros"];
        c.terminalPriceConfigured = p["terminal_price_configured"]; c.hysteresisMicros = p["hysteresis_micros"];
        if ((!c.wearSource && c.degradationMicros) || (c.flow.enabled && !c.flow.pvAcLimitW)) { return false; }
    }
    out = c; return true;
}
void serializeConfig(Config const& c, JsonObject j)
{
    j["soc_bounds_configured"] = c.socBoundsConfigured; j["enabled"] = c.enabled; j["effective_price"] = c.effectivePrice; j["export_configured"] = c.exportConfigured;
    j["dispatch_enabled"] = c.dispatchEnabled;
    j["export_micros_per_kwh"] = c.exportMicros; j["horizon_seconds"] = c.horizonSeconds; j["capacity_wh"] = c.capacityWh;
    j["min_soc_pct"] = c.minSoc; j["max_soc_pct"] = c.maxSoc; j["charge_permille"] = c.chargePermille;
    j["discharge_permille"] = c.dischargePermille; j["max_charge_w"] = c.maxChargeW; j["max_discharge_w"] = c.maxDischargeW;
    j["expected_base_load_w"] = c.baseLoadW; j["soc_step_pct"] = c.socStepPct;
    auto e = j["energy_management"].to<JsonObject>();
    e["enabled"] = c.flow.enabled; e["pv_export"] = c.flow.pvExport; e["battery_export"] = c.flow.batteryExport;
    e["ev_protection"] = c.flow.evProtection; e["preference"] = static_cast<unsigned>(c.flow.preference);
    e["max_export_w"] = c.flow.maxExportW; e["pv_ac_limit_w"] = c.flow.pvAcLimitW; e["automatic_load"] = c.automaticLoad;
    e["wear_source"] = c.wearSource; e["degradation_micros"] = c.degradationMicros; e["buffer_micros"] = c.bufferMicros;
    e["efc_capacity_wh"] = c.efcCapacityWh; e["terminal_hours"] = c.terminalHours;
    e["terminal_price_configured"] = c.terminalPriceConfigured; e["terminal_price_micros"] = c.terminalPriceMicros;
    e["hysteresis_micros"] = c.hysteresisMicros;
    auto p = j["policies"].to<JsonObject>();
    p["grid_charging_allowed"] = c.policies.gridChargingAllowed; p["discharge_allowed"] = c.policies.dischargeAllowed;
    p["target_enabled"] = c.policies.targetEnabled; p["target_soc_pct"] = c.policies.targetSoc; p["target_deadline"] = c.policies.targetDeadline;
}
bool calculate(Config const& c, Inputs const& in, Plan& out)
{
    snprintf(out.tariffSource, sizeof(out.tariffSource), "%s", in.tariff ? in.tariff->source : "");
    snprintf(out.forecastSource, sizeof(out.forecastSource), "%s", in.forecast ? in.forecast->source : "");
    snprintf(out.currency, sizeof(out.currency), "%s", in.tariff ? in.tariff->currency : "");
    out.targetState = "disabled"; out.targetDeadline = out.targetWh = 0;
    out.terminalFutureCovered = false; out.learnedSlots = 0; out.retainedByHysteresis = false;
    out.importNano = out.exportNano = out.degradationNano = out.bufferNano = out.terminalNano = out.curtailedWs = 0; out.internalAbsWh = 0;
    out.count = 0; out.pvIncluded = false; out.partial = false; out.states = 0;
    out.initialWh = out.finalWh = 0; out.runtimeUs = 0;
    out.baselineNano = out.optimizedNano = 0; out.generatedAt = in.now; out.state = "not_ready"; out.blocker = nullptr; out.partialReason = nullptr;
    auto fail = [&](char const* reason) { out.count = 0; out.blocker = reason; return false; };
    if (!c.enabled) { out.state = "disabled"; return false; }
    if (c.flow.enabled && !in.flowTopologySupported) { return fail("coordination_topology_unsupported"); }
    if (!c.socBoundsConfigured) { return fail("soc_bounds_not_configured"); }
    if (!c.capacityWh) { return fail("battery_capacity_missing"); }
    if (!c.chargePermille) { return fail("charge_efficiency_missing"); }
    if (!c.dischargePermille) { return fail("discharge_efficiency_missing"); }
    if (!c.maxChargeW || !c.maxDischargeW) { return fail("battery_power_limits_missing"); }
    // Validate even callers constructing Config directly, not only the JSON boundary.
    {
        JsonDocument checked; serializeConfig(c, checked.to<JsonObject>()); Config valid;
        if (!parseConfig(checked.as<JsonVariantConst>(), valid)) { return fail("invalid_configuration"); }
    } // Configuration JSON is scratch; release its pools before allocating DP.
    if (in.now < Energy::MinUtc || static_cast<uint64_t>(in.now) + c.horizonSeconds > UINT32_MAX) { return fail("clock_invalid"); }
    auto goals = Policies::evaluateStorageEconomicPolicy(c.policies, c.capacityWh, c.minSoc, c.maxSoc, in.now);
    out.targetDeadline = goals.deadline; out.targetWh = goals.targetWh;
    if (goals.deadline) { out.targetState = "not_ready"; }
    if (goals.blocker) { out.targetState = goals.blocker; return fail(goals.blocker); }
    auto const& storage = in.storage;
    if (!storage.device.reachable || !storage.device.enabled || !storage.device.hasUpdate || !storage.socPct.usable()
            || !std::isfinite(storage.socPct.value)) { return fail("storage_soc_not_fresh"); }
    if (storage.faultActive.usable() && storage.faultActive.value) { return fail("storage_fault_active"); }
    if (storage.socPct.value < c.minSoc || storage.socPct.value > c.maxSoc) { return fail("storage_soc_outside_configured_bounds"); }
    if (!in.tariff || !in.tariffStatus.planningUsable || !in.tariffStatus.fresh) { return fail("tariff_not_usable"); }
    out.pvIncluded = in.forecast && in.forecastStatus.planningUsable && in.forecastStatus.fresh;
    uint32_t finish = in.now + c.horizonSeconds;
    uint32_t cursor = in.now;
    // Build UTC slots before allocating DP. Stop explicitly at the first gap/size limit.
    while (cursor < finish && out.count < MaxSteps) {
        auto tariff = in.tariff->current(cursor);
        if (!tariff) { break; }
        if (c.effectivePrice && !tariff->hasImportPrice) { return fail("price_basis_unavailable"); }
        auto& slot = out.steps[out.count++]; slot = {};
        uint32_t boundary = std::min(static_cast<uint64_t>(UINT32_MAX), (static_cast<uint64_t>(cursor) / 900 + 1) * 900);
        slot.start = cursor; slot.end = std::min({ finish, tariff->end, boundary });
        // An exact deadline is a real DP boundary, including non-quarter-hour targets.
        if (goals.deadline > cursor) { slot.end = std::min(slot.end, goals.deadline); }
        slot.marketPrice = tariff->value; slot.hasImportPrice = tariff->hasImportPrice; slot.importPrice = tariff->importPrice;
        slot.usedPrice = c.effectivePrice ? tariff->importPrice : tariff->value;
        if (out.pvIncluded) {
            int64_t ws;
            if (!in.forecast->energyWattSeconds(slot.start, slot.end, ws)) { out.pvIncluded = false; }
            else if (ws < 0 || ws / (slot.end - slot.start) > 6000000) { return fail("forecast_out_of_model_bounds"); }
            else { slot.pvW = ws / (slot.end - slot.start); }
        }
        if (c.flow.enabled) {
            uint32_t physicalLimit = in.observedPvAcLimitW ? std::min(c.flow.pvAcLimitW, in.observedPvAcLimitW) : c.flow.pvAcLimitW;
            slot.pvW = std::min<int32_t>(slot.pvW, physicalLimit);
        }
        slot.loadW = c.baseLoadW;
        if (c.automaticLoad && in.loadProfile) {
            auto load = in.loadProfile->estimate(slot.start, c.baseLoadW);
            slot.loadW = load.watts; slot.loadCoverage = load.coverage; slot.loadLearned = load.learned;
            out.learnedSlots += load.learned;
        }
        cursor = slot.end;
    }
    if (!out.count) { return fail("tariff_not_usable"); }
    out.partial = cursor < finish;
    if (out.partial) { out.partialReason = out.count == MaxSteps ? "planning_slot_capacity_exceeded" : "requested_tariff_horizon_incomplete"; }
    bool targetCovered = goals.deadline && goals.deadline <= cursor;
    if (goals.deadline) {
        out.targetState = targetCovered ? "not_ready" : "target_horizon_incomplete";
        if (!targetCovered) {
            out.partial = true;
            if (!out.partialReason) { out.partialReason = "target_horizon_incomplete"; }
        }
    }
    // Exclude PV from the whole plan if any slot is uncovered; never mix unknown PV with invented zero.
    if (!out.pvIncluded) { for (size_t i = 0; i < out.count; ++i) { out.steps[i].pvW = 0; } }
    auto w = std::unique_ptr<Workspace>(new (std::nothrow) Workspace());
    if (!w) { return fail("insufficient_memory"); }
    uint32_t lower = goals.reserveWh, upper = goals.ceilingWh;
    out.initialWh = std::llround(storage.socPct.value * c.capacityWh / 100.0);
    if (out.initialWh < lower || out.initialWh > upper) { return fail("storage_soc_outside_configured_bounds"); }
    uint32_t step = std::max(static_cast<uint32_t>(1), static_cast<uint32_t>((static_cast<uint64_t>(c.capacityWh) * c.socStepPct + 99) / 100));
    for (uint32_t e = lower; e < upper; e += step) {
        if (out.states >= MaxStates - 3) { return fail("too_many_soc_states"); }
        w->energy[out.states++] = e;
    }
    w->energy[out.states++] = upper; w->energy[out.states++] = out.initialWh;
    if (targetCovered) { w->energy[out.states++] = goals.targetWh; }
    std::sort(w->energy, w->energy + out.states);
    out.states = std::unique(w->energy, w->energy + out.states) - w->energy;
    size_t initial = std::lower_bound(w->energy, w->energy + out.states, out.initialWh) - w->energy;
    // A bounded continuation table replaces the moving start-energy floor.
    // Credible covered future tariffs cap inventory at its future refill value.
    // Missing tail tariffs use a transparent robust covered-price assumption,
    // independent of the current SoC. Scratch reuses the not-yet-used parents.
    uint64_t loadWs = 0; uint32_t duration = 0;
    for (size_t n = 0; n < out.count; ++n) {
        loadWs += static_cast<uint64_t>(out.steps[n].loadW) * (out.steps[n].end - out.steps[n].start);
        duration += out.steps[n].end - out.steps[n].start;
    }
    uint64_t tailEnd = static_cast<uint64_t>(cursor) + c.terminalHours * 3600;
    uint32_t tailCursor = cursor; size_t priceCount = 0;
    while (tailCursor < tailEnd && priceCount < MaxSteps && tailEnd <= UINT32_MAX) {
        auto price = in.tariff->current(tailCursor);
        if (!price || (c.effectivePrice && !price->hasImportPrice)) { break; }
        w->priceScratch[priceCount++] = c.effectivePrice ? price->importPrice : price->value;
        tailCursor = std::min<uint64_t>(price->end, tailEnd);
    }
    out.terminalFutureCovered = tailCursor == tailEnd && priceCount;
    if (!out.terminalFutureCovered) {
        priceCount = out.count;
        for (size_t n = 0; n < out.count; ++n) { w->priceScratch[n] = out.steps[n].usedPrice; }
    }
    std::sort(w->priceScratch, w->priceScratch + priceCount);
    int32_t assumedPrice = w->priceScratch[out.terminalFutureCovered && goals.gridChargingAllowed ? 0 : (priceCount - 1) / 2];
    out.terminalPrice = c.terminalPriceConfigured ? c.terminalPriceMicros : std::max(0, assumedPrice);
    out.terminalDemandWh = std::min<uint64_t>(upper - lower,
        (loadWs / std::max<uint32_t>(1, duration)) * c.terminalHours * 1000 / c.dischargePermille);
    for (size_t n = 0; n < out.states; ++n) { w->terminal[n] = terminalValue(c, out, w->energy[n]); }
    w->previous[initial] = { 0, 0 };
    for (size_t t = 0; t < out.count; ++t) {
        std::fill(std::begin(w->next), std::end(w->next), Score {});
        auto& slot = out.steps[t];
        Flow baseline; transition(c, goals, in, slot, t, out.initialWh, out.initialWh, baseline); out.baselineNano += baseline.cost / 2;
        for (size_t from = 0; from < out.states; ++from) {
            if (w->previous[from].cost == Infinity) { continue; }
            for (size_t to = 0; to < out.states; ++to) {
                // At the deadline the target is a lower bound, not a price-ranking heuristic.
                if (targetCovered && slot.end == goals.deadline && w->energy[to] < goals.targetWh) { continue; }
                Flow f;
                if (!transition(c, goals, in, slot, t, w->energy[from], w->energy[to], f)) { continue; }
                Score candidate { w->previous[from].cost + f.cost,
                    w->previous[from].throughput + static_cast<uint64_t>(std::abs(static_cast<int64_t>(w->energy[to]) - w->energy[from])) };
                if (better(candidate, w->next[to])) { w->next[to] = candidate; w->parent[t][to] = from; }
            }
        }
        std::copy(std::begin(w->next), std::end(w->next), std::begin(w->previous));
    }
    size_t selected = 0; Score best;
    for (size_t i = 0; i < out.states; ++i) {
        if (w->previous[i].cost == Infinity) { continue; }
        Score candidate { w->previous[i].cost + 2 * w->terminal[i], w->previous[i].throughput };
        if (better(candidate, best)) { best = candidate; selected = i; }
    }
    if (w->previous[selected].cost == Infinity) {
        out.targetState = targetCovered ? "target_unreachable" : out.targetState;
        return fail(targetCovered ? "target_unreachable" : "no_feasible_plan");
    }
    out.finalWh = w->energy[selected];
    out.terminalNano = w->terminal[selected] - terminalValue(c, out, out.initialWh);
    out.optimizedNano = w->previous[selected].cost / 2 + out.terminalNano;
    for (size_t t = out.count; t-- > 0;) {
        size_t from = w->parent[t][selected]; auto& slot = out.steps[t]; Flow flow;
        transition(c, goals, in, slot, t, w->energy[from], w->energy[selected], flow);
        slot.startWh = w->energy[from]; slot.endWh = w->energy[selected];
        applyFlow(slot, flow);
        selected = from;
    }
    totals(c, out);
    if (targetCovered) { out.targetState = "target_planned"; }
    out.state = out.partial ? "partial" : "ready"; return true;
}
bool evaluateRetained(Config const& c, Inputs const& in, Plan const& candidate, Plan const& old, Plan& out, int64_t retentionLimitNano)
{
    if (!candidate.count || !old.count) { return false; }
    auto goals = Policies::evaluateStorageEconomicPolicy(c.policies, c.capacityWh, c.minSoc, c.maxSoc, in.now);
    uint32_t energy = candidate.initialWh; int64_t cost = 0;
    for (unsigned pass = 0; pass < 2; ++pass) {
        energy = candidate.initialWh; cost = 0;
        for (size_t n = 0; n < candidate.count; ++n) {
            auto const& slot = candidate.steps[n]; Step const* retained = nullptr;
            for (size_t k = 0; k < old.count; ++k) if (old.steps[k].start <= slot.start && old.steps[k].end >= slot.end) { retained = &old.steps[k]; break; }
            // Complete a newly exposed horizon tail with the fresh candidate's
            // action. The retained current slot itself must still be covered.
            if (!retained && n && slot.end > old.steps[old.count - 1].end) { retained = &candidate.steps[n]; }
            if (!retained) { return false; }
            int64_t delta = (static_cast<int64_t>(retained->endWh) - retained->startWh) * (slot.end - slot.start) / (retained->end - retained->start);
            int64_t next = static_cast<int64_t>(energy) + delta;
            if (next < goals.reserveWh || next > goals.ceilingWh || (goals.deadline == slot.end && next < goals.targetWh)) { return false; }
            Flow f; if (!transition(c, goals, in, slot, n, energy, next, f)) { return false; }
            if (pass) { out.steps[n].startWh = energy; out.steps[n].endWh = next; applyFlow(out.steps[n], f); }
            cost += f.cost; energy = next;
        }
        int64_t terminal = terminalValue(c, candidate, energy) - terminalValue(c, candidate, candidate.initialWh);
        if (!pass) {
            if (retentionLimitNano != INT64_MAX && cost + 2 * terminal - 2 * candidate.optimizedNano > 2 * retentionLimitNano) { return true; }
            out = candidate;
        } else {
            out.finalWh = energy; out.terminalNano = terminal; out.optimizedNano = cost / 2 + terminal;
            totals(c, out); out.retainedByHysteresis = true;
        }
    }
    return true;
}
void serializePlan(Config const& c, Inputs const& in, Plan const& p, JsonObject out, size_t limit, size_t offset)
{
    serializeConfig(c, out["config"].to<JsonObject>());
    out["shadow_only"] = true; out["device_writes"] = false; out["state"] = p.state;
    out["generated_at"] = p.generatedAt; out["pv_included"] = p.pvIncluded; out["partial_horizon"] = p.partial;
    for (auto key : { "initial_soc_pct", "final_soc_pct", "baseline_cost_nano", "optimized_cost_nano", "modeled_savings_nano" }) { out[key] = nullptr; }
    if (p.count) {
        out["initial_soc_pct"] = 100.0 * p.initialWh / c.capacityWh; out["final_soc_pct"] = 100.0 * p.finalWh / c.capacityWh;
        out["baseline_cost_nano"] = p.baselineNano; out["optimized_cost_nano"] = p.optimizedNano;
        out["modeled_savings_nano"] = p.baselineNano - p.optimizedNano;
    }
    auto costs = out["cost_breakdown"].to<JsonObject>();
    costs["import_nano"] = p.importNano; costs["export_nano"] = p.exportNano;
    costs["degradation_nano"] = p.degradationNano; costs["buffer_nano"] = p.bufferNano;
    costs["terminal_inventory_change_nano"] = p.terminalNano; costs["curtailed_wh"] = static_cast<double>(p.curtailedWs) / 3600;
    costs["efc_reference_kwh"] = static_cast<double>(p.internalAbsWh) / 2000;
    costs["efc"] = (c.efcCapacityWh ? c.efcCapacityWh : c.capacityWh) ? static_cast<double>(p.internalAbsWh) / (2.0 * (c.efcCapacityWh ? c.efcCapacityWh : c.capacityWh)) : 0;
    costs["wear_source"] = c.wearSource; costs["terminal_price_micros"] = p.terminalPrice;
    costs["terminal_demand_wh"] = p.terminalDemandWh;
    costs["terminal_source"] = c.terminalPriceConfigured ? "explicit" : p.terminalFutureCovered ? "covered_future_tariffs" : "covered_price_median_assumption";
    out["projected_grid_bill_difference_nano"] = p.baselineNano - (p.importNano - p.exportNano);
    uint32_t peakWh = p.initialWh;
    for (size_t n = 0; n < p.count; ++n) { peakWh = std::max(peakWh, p.steps[n].endWh); }
    out["charge_target_soc_pct"] = c.capacityWh ? static_cast<double>(peakWh) * 100 / c.capacityWh : 0;
    out["charge_target_reason"] = "load_pv_cost_and_constraints";
    out["load_forecast_source"] = p.learnedSlots ? "local_profile" : "constant_fallback";
    out["load_learned_slots"] = p.learnedSlots; out["load_total_slots"] = p.count;
    out["retained_by_hysteresis"] = p.retainedByHysteresis;
    out["soc_states"] = p.states; out["calculation_us"] = p.runtimeUs;
    out["price_basis"] = c.effectivePrice ? "effective_variable_import" : "source_market";
    out["tariff_source"] = static_cast<char const*>(p.tariffSource);
    out["forecast_source"] = p.pvIncluded ? static_cast<char const*>(p.forecastSource) : "excluded";
    out["currency"] = static_cast<char const*>(p.currency);
    out["tariff_fetched_at"] = in.tariffStatus.fetchedAt; out["forecast_fetched_at"] = in.forecastStatus.fetchedAt;
    out["tariff_generated_at"] = in.tariffStatus.sourceAt; out["forecast_generated_at"] = in.forecastStatus.sourceAt;
    out["storage_age_ms"] = in.storage.device.ageMs;
    out["target_state"] = p.targetState; out["target_deadline"] = p.targetDeadline;
    out["target_soc_pct"] = c.policies.targetEnabled ? c.policies.targetSoc : 0;
    auto restrictions = out["policy_restrictions"].to<JsonArray>();
    if (!c.flow.batteryExport) { restrictions.add("battery_export_forbidden"); }
    if (!c.flow.pvExport) { restrictions.add("pv_export_forbidden"); }
    if (!c.policies.gridChargingAllowed) { restrictions.add("grid_charging_forbidden"); }
    if (!c.policies.dischargeAllowed) { restrictions.add("economic_discharge_forbidden"); }
    if (!in.currentConstraints.chargeAllowed) { restrictions.add("current_charge_restricted"); }
    if (!in.currentConstraints.dischargeAllowed) { restrictions.add("current_discharge_restricted"); }
    auto blockers = out["blockers"].to<JsonArray>(); if (p.blocker) { blockers.add(p.blocker); }
    auto assumptions = out["assumptions"].to<JsonArray>();
    assumptions.add(c.automaticLoad ? "local_profile_with_explicit_fallback" : "constant_explicit_base_load"); assumptions.add("bounded_terminal_inventory_value");
    assumptions.add("future_ev_constraints_unknown"); assumptions.add("monthly_fee_excluded"); assumptions.add(c.wearSource ? "configured_marginal_cycle_cost" : "battery_wear_unknown");
    auto warnings = out["warnings"].to<JsonArray>(); warnings.add("modeled_savings_not_guaranteed");
    if (!c.wearSource) { warnings.add("battery_wear_unknown"); }
    if (!p.pvIncluded) { warnings.add("pv_forecast_excluded"); warnings.add(in.forecastStatus.fresh ? "pv_horizon_incomplete" : "pv_unavailable_or_stale"); }
    if (p.partial) { warnings.add(p.partialReason); }
    if (p.targetDeadline && !strcmp(p.targetState, "target_horizon_incomplete")
            && p.partialReason && strcmp(p.partialReason, "target_horizon_incomplete")) { warnings.add("target_horizon_incomplete"); }
    if (c.policies.targetEnabled) {
        warnings.add("target_is_modeled_not_guaranteed");
        if (p.count && p.initialWh < p.targetWh) { warnings.add("hold_baseline_does_not_meet_target"); }
    }

    if (in.tariffStatus.error) { warnings.add("tariff_refresh_failed_retained_intervals_used"); }
    out["current_charge_allowed"] = in.currentConstraints.chargeAllowed;
    out["current_charge_limit_w"] = nullptr; out["current_discharge_limit_w"] = nullptr;
    if (in.currentConstraints.maxChargePower.active) { out["current_charge_limit_w"] = in.currentConstraints.maxChargePower.valueW; }
    if (in.currentConstraints.maxDischargePower.active) { out["current_discharge_limit_w"] = in.currentConstraints.maxDischargePower.valueW; }
    out["current_discharge_allowed"] = in.currentConstraints.dischargeAllowed;
    out["tariff_usable"] = in.tariffStatus.planningUsable; out["storage_soc_fresh"] = in.storage.socPct.usable() && in.storage.device.reachable;
    out["horizon_start"] = p.count ? p.steps[0].start : 0; out["horizon_end"] = p.count ? p.steps[p.count - 1].end : 0;
    out["interval_count"] = p.count;
    auto schedule = out["schedule"].to<JsonArray>();
    limit = std::min(limit, static_cast<size_t>(24));
    out["schedule_offset"] = offset; out["has_more"] = offset + limit < p.count;
    for (size_t n = offset; n < p.count && n - offset < std::min(limit, size_t(24)); ++n) {
        auto const& s = p.steps[n]; auto j = schedule.add<JsonObject>();
        j["start"] = s.start; j["end"] = s.end; j["market_price_micros_per_kwh"] = s.marketPrice;
        j["import_price_micros_per_kwh"] = nullptr; if (s.hasImportPrice) { j["import_price_micros_per_kwh"] = s.importPrice; }
        j["used_price_micros_per_kwh"] = s.usedPrice; j["pv_w"] = s.pvW;
        j["pv_wh"] = static_cast<double>(s.pvW) * (s.end - s.start) / 3600; j["assumed_load_w"] = s.loadW; j["load_coverage_pct"] = s.loadCoverage; j["load_learned"] = s.loadLearned;
        j["action"] = s.endWh > s.startWh ? "charge" : s.endWh < s.startWh ? "discharge" : "hold";
        j["battery_w"] = s.batteryW; j["start_soc_pct"] = 100.0 * s.startWh / c.capacityWh;
        j["end_soc_pct"] = 100.0 * s.endWh / c.capacityWh; j["grid_import_wh"] = static_cast<double>(s.gridImportWs) / 3600;
        j["grid_export_wh"] = static_cast<double>(s.gridExportWs) / 3600; j["cost_nano"] = s.costNano;
        j["reason"] = s.endWh == s.startWh ? "least_cost_hold_with_policies" : "horizon_cost_minimum_with_policies_and_losses";
    }
}
} // namespace Hems::Optimizer
