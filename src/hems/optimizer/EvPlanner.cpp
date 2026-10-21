// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/EvPlanner.h>
#include <algorithm>
#include <cstring>
#include <limits>
namespace Hems::Optimizer {
namespace {
bool number(JsonVariantConst j, char const* key, int64_t low, int64_t high)
{
    return j[key].is<int64_t>() && j[key].as<int64_t>() >= low && j[key].as<int64_t>() <= high;
}
int64_t cost(int64_t ws, int32_t price)
{
    // nano-currency: split before multiplication to bound all model inputs.
    return (ws / 3600) * price + (ws % 3600) * price / 3600;
}
uint32_t power(EvConfig const& c, uint16_t current) { return static_cast<uint32_t>(c.voltage) * c.phases * current; }
int64_t energy(EvConfig const& c, EvStep const& s)
{
    return static_cast<int64_t>(power(c, s.currentA)) * (s.chargeEnd - s.start);
}
} // namespace
bool parseEvConfig(JsonVariantConst j, EvConfig& out)
{
    if (!j.is<JsonObjectConst>() || !j["enabled"].is<bool>() || !j["pv_preferred"].is<bool>()
        || !j["max_price_enabled"].is<bool>() || !number(j, "deadline", 0, UINT32_MAX)
        || !number(j, "required_wh", 0, 1000000) || !number(j, "min_current_a", 6, 80)
        || !number(j, "max_current_a", 6, 80) || !number(j, "nominal_voltage", 0, 250)
        || !number(j, "phases", 0, 3) || !number(j, "max_price_micros_per_kwh", INT32_MIN, INT32_MAX)) { return false; }
    EvConfig c;
    c.enabled = j["enabled"]; c.pvPreferred = j["pv_preferred"]; c.maxPriceEnabled = j["max_price_enabled"];
    c.deadline = j["deadline"]; c.requiredWh = j["required_wh"];
    c.minCurrentA = j["min_current_a"]; c.maxCurrentA = j["max_current_a"];
    c.voltage = j["nominal_voltage"]; c.phases = j["phases"]; c.maxPriceMicros = j["max_price_micros_per_kwh"];
    if (c.minCurrentA > c.maxCurrentA || (c.voltage && c.voltage < 100)
        || (c.enabled && c.deadline < Energy::MinUtc)) { return false; }
    out = c; return true;
}
void serializeEvConfig(EvConfig const& c, JsonObject j)
{
    j["enabled"] = c.enabled; j["pv_preferred"] = c.pvPreferred; j["deadline"] = c.deadline;
    j["required_wh"] = c.requiredWh; j["min_current_a"] = c.minCurrentA; j["max_current_a"] = c.maxCurrentA;
    j["nominal_voltage"] = c.voltage; j["phases"] = c.phases;
    j["max_price_enabled"] = c.maxPriceEnabled; j["max_price_micros_per_kwh"] = c.maxPriceMicros;
}
bool calculateEv(Config const& config, Inputs const& in, Plan const& storage, EvPlan& out)
{
    std::fill(std::begin(out.steps), std::end(out.steps), EvStep {});
    out.count = 0; out.generatedAt = in.now; out.coverageEnd = out.completion = 0;
    out.plannedWs = out.deliverableWs = out.costNano = 0;
    out.pvIncluded = out.storageIncluded = out.storageConflict = false;
    out.reason = "disabled"; out.coverageReason = nullptr;
    auto const& c = config.ev;
    auto fail = [&](char const* why) { out.count = 0; out.plannedWs = out.deliverableWs = out.costNano = 0; out.reason = why; return false; };
    if (!c.enabled) { return fail("disabled"); }
    JsonDocument checked; serializeEvConfig(c, checked.to<JsonObject>()); EvConfig valid;
    if (!parseEvConfig(checked.as<JsonVariantConst>(), valid) || config.horizonSeconds < 900
        || config.horizonSeconds > 172800 || config.baseLoadW > 100000) { return fail("invalid_configuration"); }
    if (in.now < Energy::MinUtc) { return fail("clock_invalid"); }
    if (c.deadline <= in.now) { return fail("deadline_passed"); }
    if (!c.requiredWh) { out.reason = "energy_not_required"; out.completion = in.now; return true; }
    if (!c.voltage || !c.phases) { return fail("electrical_model_missing"); }
    if (!in.tariff || !in.tariffStatus.fresh || !in.tariffStatus.planningUsable) { return fail("tariff_not_usable"); }
    // A configured storage optimizer must supply its real projection. Never invent a battery schedule.
    if (config.enabled && (!storage.count || storage.blocker)) { return fail("storage_projection_unavailable"); }
    out.storageIncluded = config.enabled;
    uint32_t finish = std::min(static_cast<uint64_t>(c.deadline), static_cast<uint64_t>(in.now) + config.horizonSeconds);
    uint32_t cursor = in.now;
    out.pvIncluded = c.pvPreferred && in.forecast && in.forecastStatus.fresh && in.forecastStatus.planningUsable
        && in.forecast->covers(cursor, finish) && (!config.enabled || storage.pvIncluded);
    size_t storageIndex = 0;
    while (cursor < finish && out.count < MaxSteps) {
        auto tariff = in.tariff->current(cursor);
        if (!tariff) { out.coverageReason = "tariff_gap_or_horizon_incomplete"; break; }
        if (config.effectivePrice && !tariff->hasImportPrice) { out.coverageReason = "price_basis_unavailable"; break; }
        auto& s = out.steps[out.count];
        s.start = cursor; s.chargeEnd = cursor;
        s.end = std::min({finish, tariff->end, static_cast<uint32_t>(std::min(static_cast<uint64_t>(UINT32_MAX), (static_cast<uint64_t>(cursor) / 900 + 1) * 900))});
        s.price = config.effectivePrice ? tariff->importPrice : tariff->value;
        int32_t pvW = 0;
        if (out.pvIncluded) {
            auto pv = in.forecast->current(cursor);
            if (pv->value < 0 || pv->value > 6000000) { return fail("forecast_out_of_model_bounds"); }
            s.end = std::min(s.end, pv->end); pvW = pv->value;
        }
        if (config.enabled) {
            while (storageIndex < storage.count && storage.steps[storageIndex].end <= cursor) { ++storageIndex; }
            if (storageIndex == storage.count || storage.steps[storageIndex].start > cursor) {
                out.coverageReason = "storage_projection_incomplete"; break;
            }
            auto const& projected = storage.steps[storageIndex];
            s.end = std::min(s.end, projected.end); s.storageW = projected.batteryW;
        }
        s.residualPvW = std::max(static_cast<int32_t>(0), pvW - static_cast<int32_t>(config.baseLoadW) - std::max(static_cast<int32_t>(0), s.storageW));
        // Do not present planned battery discharge as free EV energy. Preserve storage's schedule.
        if (s.storageW < 0) { out.storageConflict = true; }
        else if (!c.maxPriceEnabled || s.price <= c.maxPriceMicros || s.residualPvW >= static_cast<int32_t>(power(c, c.minCurrentA))) {
            // A price cap restricts grid energy. Above it, only a wholly PV-covered current is feasible.
            uint16_t allowed = c.maxCurrentA;
            if (c.maxPriceEnabled && s.price > c.maxPriceMicros) {
                allowed = std::min(static_cast<uint32_t>(allowed), static_cast<uint32_t>(s.residualPvW) / power(c, 1));
            }
            if (allowed >= c.minCurrentA) {
                s.currentA = allowed;
                out.deliverableWs += static_cast<int64_t>(power(c, allowed)) * (s.end - s.start);
            }
        }
        ++out.count; cursor = s.end;
    }
    out.coverageEnd = cursor;
    if (!out.coverageReason && cursor < c.deadline) {
        out.coverageReason = out.count == MaxSteps ? "planning_slot_capacity_exceeded" : "planning_horizon_incomplete";
    }
    if (!out.count) { return fail(out.coverageReason ? out.coverageReason : "tariff_not_usable"); }
    // Bounded greedy residual model. PV preference ranks PV coverage first, then exact marginal cost;
    // ties always prefer earlier UTC intervals. This is not a joint-home optimum.
    uint16_t order[MaxSteps];
    for (uint16_t i = 0; i < out.count; ++i) { order[i] = i; }
    auto fraction = [&](EvStep const& s) -> int64_t {
        return s.currentA ? std::min(static_cast<int64_t>(1000000), static_cast<int64_t>(s.residualPvW) * 1000000 / power(c, s.currentA)) : 0;
    };
    std::sort(order, order + out.count, [&](uint16_t a, uint16_t b) {
        auto const& x = out.steps[a]; auto const& y = out.steps[b];
        auto fx = fraction(x), fy = fraction(y);
        if (c.pvPreferred && fx != fy) { return fx > fy; }
        auto marginal = [&](EvStep const& s, int64_t f) {
            return static_cast<int64_t>(s.price) * (1000000 - f) + (config.exportConfigured ? static_cast<int64_t>(config.exportMicros) * f : 0);
        };
        auto cx = marginal(x, fx), cy = marginal(y, fy);
        return cx == cy ? x.start < y.start : cx < cy;
    });
    int64_t remaining = static_cast<int64_t>(c.requiredWh) * 3600;
    for (size_t i = 0; i < out.count; ++i) {
        auto& s = out.steps[order[i]];
        if (!s.currentA || remaining <= 0) { s.currentA = 0; continue; }
        uint32_t watts = power(c, s.currentA);
        // Partial final interval at a permitted current, rounded up by less than one second of energy.
        uint32_t seconds = std::min(static_cast<int64_t>(s.end - s.start), (remaining + watts - 1) / watts);
        s.chargeEnd = s.start + seconds;
        auto ws = energy(c, s); remaining -= ws; out.plannedWs += ws;
        auto pvWs = std::min(ws, static_cast<int64_t>(s.residualPvW) * seconds);
        s.costNano = cost(ws - pvWs, s.price) + (config.exportConfigured ? cost(pvWs, config.exportMicros) : 0);
        out.costNano += s.costNano;
        out.completion = std::max(out.completion, s.chargeEnd);
    }
    if (remaining > 0) { out.completion = 0; }
    out.reason = remaining > 0 ? "energy_shortfall" : "planned";
    return true;
}
void serializeEv(Config const& config, Inputs const& in, EvPlan const& p, JsonObject j,
    uint32_t now, uint32_t revision, size_t offset, bool stale)
{
    auto const& c = config.ev;
    serializeEvConfig(c, j["config"].to<JsonObject>());
    j["enabled"] = c.enabled; j["shadow_only"] = true; j["device_writes"] = false;
    j["plan_revision"] = revision; j["storage_plan_revision"] = revision;
    j["generated_at"] = p.generatedAt; j["deadline"] = c.deadline; j["required_wh"] = c.requiredWh;
    j["reason"] = stale ? "inputs_changed_recalculation_required" : p.reason;
    j["stale_inputs"] = stale; j["coverage_reason"] = p.coverageReason;
    j["tariff_coverage_start"] = p.generatedAt; j["tariff_coverage_end"] = p.coverageEnd;
    j["tariff_fresh"] = in.tariffStatus.fresh; j["pv_fresh"] = in.forecastStatus.fresh;
    j["tariff_fetched_at"] = in.tariffStatus.fetchedAt; j["pv_fetched_at"] = in.forecastStatus.fetchedAt;
    j["pv_coverage_start"] = nullptr; j["pv_coverage_end"] = nullptr;
    if (p.pvIncluded) { j["pv_coverage_start"] = p.generatedAt; j["pv_coverage_end"] = p.coverageEnd; }
    j["pv_included"] = p.pvIncluded; j["storage_projection_used"] = p.storageIncluded;
    j["storage_conflict"] = p.storageConflict; j["model"] = "staged_residual";
    j["storage_interaction"] = !config.enabled ? "storage_planner_disabled" : p.storageConflict
        ? "storage_discharge_slots_excluded" : "storage_schedule_preserved";
    j["currency"] = static_cast<char const*>(in.tariff ? in.tariff->currency : "");
    j["planned_wh"] = j["deliverable_wh"] = j["shortfall_wh"] = j["cost_nano"] = j["completion"] = nullptr;
    j["current_action"] = "wait"; j["current_a"] = 0; j["next_change"] = nullptr; j["next_action"] = "wait";
    auto rows = j["schedule"].to<JsonArray>();
    j["interval_count"] = stale ? 0 : p.count; j["schedule_offset"] = offset; j["has_more"] = !stale && offset + 24 < p.count;
    if (stale || (strcmp(p.reason, "planned") && strcmp(p.reason, "energy_shortfall")
            && strcmp(p.reason, "energy_not_required"))) { return; }
    j["planned_wh"] = static_cast<double>(p.plannedWs) / 3600; j["deliverable_wh"] = static_cast<double>(p.deliverableWs) / 3600;
    j["shortfall_wh"] = std::max(0.0, static_cast<double>(c.requiredWh) - static_cast<double>(p.plannedWs) / 3600);
    j["cost_nano"] = p.costNano; if (p.completion) { j["completion"] = p.completion; }
    auto currentAt = [&](uint32_t at) -> uint16_t {
        for (size_t i = 0; i < p.count; ++i) {
            auto const& step = p.steps[i];
            if (at >= step.start && at < step.chargeEnd) { return step.currentA; }
        }
        return 0;
    };
    auto current = currentAt(now);
    j["current_action"] = current ? "charge" : "wait"; j["current_a"] = current;
    uint32_t next = 0; uint16_t nextCurrent = 0;
    auto consider = [&](uint32_t boundary, uint16_t amps) {
        if (boundary > now && amps != current && (!next || boundary < next)) { next = boundary; nextCurrent = amps; }
    };
    for (size_t i = 0; i < p.count; ++i) {
        auto const& s = p.steps[i];
        consider(s.start, s.currentA);
        if (s.currentA) {
            auto const* after = i + 1 < p.count ? &p.steps[i + 1] : nullptr;
            auto amps = after && after->start == s.chargeEnd ? after->currentA : 0;
            consider(s.chargeEnd, amps);
        }
    }
    if (next) { j["next_change"] = next; j["next_action"] = nextCurrent ? "charge" : "wait"; j["next_current_a"] = nextCurrent; }
    for (size_t i = 0; i < p.count; ++i) {
        auto const& s = p.steps[i];
        if (i < offset || i >= offset + 24) { continue; }
        auto row = rows.add<JsonObject>(); row["start"] = s.start; row["end"] = s.end;
        row["charge_end"] = s.chargeEnd; row["current_a"] = s.currentA;
        row["action"] = s.currentA ? "charge" : "wait"; row["energy_wh"] = static_cast<double>(energy(c, s)) / 3600;
        row["used_price_micros_per_kwh"] = s.price; row["residual_pv_w"] = s.residualPvW;
        row["storage_w"] = s.storageW; row["cost_nano"] = s.costNano;
    }
}
} // namespace Hems::Optimizer
