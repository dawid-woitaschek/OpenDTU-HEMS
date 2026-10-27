// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Planner.h>
#include <hems/Memory.h>
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
struct Score { int64_t cost = Infinity; uint64_t throughput = UINT64_MAX; };
bool better(Score a, Score b) { return a.cost < b.cost || (a.cost == b.cost && a.throughput < b.throughput); }
struct Workspace {
    Score previous[MaxStates], next[MaxStates];
    uint8_t parent[MaxSteps][MaxStates] {};
    uint32_t energy[MaxStates] {};
};
// nano currency = price micro currency/kWh * Ws / 3600. Split before multiplication.
int64_t money(int64_t ws, int32_t price) { return (ws / 3600) * price + ((ws % 3600) * price) / 3600; }
struct Flow { int64_t batteryWs = 0, importWs = 0, exportWs = 0, cost = 0; };
bool transition(Config const& c, Policies::StorageEconomicGoals const& goals, Inputs const& in, Step const& slot, size_t index, uint32_t from, uint32_t to, Flow& flow)
{
    uint32_t seconds = slot.end - slot.start;
    int64_t deltaWs = (static_cast<int64_t>(to) - from) * 3600;
    int64_t netWs = (static_cast<int64_t>(c.baseLoadW) - slot.pvW) * seconds;
    uint32_t chargeW = c.maxChargeW, dischargeW = c.maxDischargeW;
    if (!index) {
        auto const& constraints = in.currentConstraints;
        if (!constraints.chargeAllowed) { chargeW = 0; }
        if (!constraints.dischargeAllowed) { dischargeW = 0; }
        if (constraints.maxChargePower.active) { chargeW = std::min(static_cast<double>(chargeW), std::max(0.0, constraints.maxChargePower.valueW)); }
        if (constraints.maxDischargePower.active) { dischargeW = std::min(static_cast<double>(dischargeW), std::max(0.0, constraints.maxDischargePower.valueW)); }
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
        if (!c.exportConfigured && -flow.batteryWs > std::max(static_cast<int64_t>(0), netWs)) { return false; }
    }
    int64_t grid = netWs + flow.batteryWs;
    flow.importWs = std::max(static_cast<int64_t>(0), grid); flow.exportWs = std::max(static_cast<int64_t>(0), -grid);
    flow.cost = money(flow.importWs, slot.usedPrice) - (c.exportConfigured ? money(flow.exportWs, c.exportMicros) : 0);
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
    out.count = 0; out.pvIncluded = false; out.partial = false; out.states = 0;
    out.initialWh = out.finalWh = 0; out.runtimeUs = 0;
    out.baselineNano = out.optimizedNano = 0; out.generatedAt = in.now; out.state = "not_ready"; out.blocker = nullptr; out.partialReason = nullptr;
    auto fail = [&](char const* reason) { out.count = 0; out.blocker = reason; return false; };
    if (!c.enabled) { out.state = "disabled"; return false; }
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
    auto w = Memory::make<Workspace>();
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
    w->previous[initial] = { 0, 0 };
    for (size_t t = 0; t < out.count; ++t) {
        std::fill(std::begin(w->next), std::end(w->next), Score {});
        auto& slot = out.steps[t];
        Flow baseline; transition(c, goals, in, slot, t, out.initialWh, out.initialWh, baseline); out.baselineNano += baseline.cost;
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
    // Terminal reserve: no free depletion of initial stock to manufacture horizon-end savings.
    size_t selected = initial;
    for (size_t i = initial; i < out.states; ++i) { if (better(w->previous[i], w->previous[selected])) { selected = i; } }
    if (w->previous[selected].cost == Infinity) {
        out.targetState = targetCovered ? "target_unreachable" : out.targetState;
        return fail(targetCovered ? "target_unreachable" : "no_feasible_plan");
    }
    out.finalWh = w->energy[selected]; out.optimizedNano = w->previous[selected].cost;
    for (size_t t = out.count; t-- > 0;) {
        size_t from = w->parent[t][selected]; auto& slot = out.steps[t]; Flow flow;
        transition(c, goals, in, slot, t, w->energy[from], w->energy[selected], flow);
        slot.startWh = w->energy[from]; slot.endWh = w->energy[selected];
        slot.batteryW = flow.batteryWs / (slot.end - slot.start);
        slot.gridImportWs = flow.importWs; slot.gridExportWs = flow.exportWs; slot.costNano = flow.cost;
        selected = from;
    }
    if (targetCovered) { out.targetState = "target_planned"; }
    out.state = out.partial ? "partial" : "ready"; return true;
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
    if (!c.policies.gridChargingAllowed) { restrictions.add("grid_charging_forbidden"); }
    if (!c.policies.dischargeAllowed) { restrictions.add("economic_discharge_forbidden"); }
    if (!in.currentConstraints.chargeAllowed) { restrictions.add("current_charge_restricted"); }
    if (!in.currentConstraints.dischargeAllowed) { restrictions.add("current_discharge_restricted"); }
    auto blockers = out["blockers"].to<JsonArray>(); if (p.blocker) { blockers.add(p.blocker); }
    auto assumptions = out["assumptions"].to<JsonArray>();
    assumptions.add("constant_explicit_base_load"); assumptions.add("terminal_soc_at_least_initial");
    assumptions.add("future_ev_constraints_unknown"); assumptions.add("monthly_fee_excluded"); assumptions.add("no_battery_degradation_model");
    auto warnings = out["warnings"].to<JsonArray>(); warnings.add("modeled_savings_not_guaranteed");
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
        j["pv_wh"] = static_cast<double>(s.pvW) * (s.end - s.start) / 3600; j["assumed_load_w"] = c.baseLoadW;
        j["action"] = s.endWh > s.startWh ? "charge" : s.endWh < s.startWh ? "discharge" : "hold";
        j["battery_w"] = s.batteryW; j["start_soc_pct"] = 100.0 * s.startWh / c.capacityWh;
        j["end_soc_pct"] = 100.0 * s.endWh / c.capacityWh; j["grid_import_wh"] = static_cast<double>(s.gridImportWs) / 3600;
        j["grid_export_wh"] = static_cast<double>(s.gridExportWs) / 3600; j["cost_nano"] = s.costNano;
        j["reason"] = s.endWh == s.startWh ? "least_cost_hold_with_policies" : "horizon_cost_minimum_with_policies_and_losses";
    }
}
} // namespace Hems::Optimizer
