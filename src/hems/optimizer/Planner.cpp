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
#include <numeric>

namespace Hems::Optimizer {
namespace {
bool number(JsonVariantConst j, char const* key, uint32_t lo, uint32_t hi) {
    auto v = j[key]; return v.is<uint32_t>() && v.as<uint32_t>() >= lo && v.as<uint32_t>() <= hi;
}
constexpr int64_t Infinity = std::numeric_limits<int64_t>::max() / 4;
// DP objective is in half-nano currency to retain the half-throughput convention.
struct Score { int64_t cost = Infinity; uint64_t throughput = UINT64_MAX; };
bool better(Score a, Score b) { return a.cost < b.cost || (a.cost == b.cost && a.throughput < b.throughput); }
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
bool batteryExportAllowed(Config const& c) { return c.flow.enabled ? c.flow.batteryExport : c.exportConfigured; }
bool pvChargeReachable(Config const& c, Inputs const& in) {
    return !c.flow.enabled || c.policies.gridChargingAllowed || (c.flow.pvExport && c.flow.maxExportW) || in.pvSurplusObserved;
}
struct PowerLimits { uint32_t chargeW, dischargeW; };
PowerLimits powerLimits(Config const& c, Inputs const& in, Step const& slot, size_t index)
{
    uint32_t chargeW = c.maxChargeW, dischargeW = c.maxDischargeW;
    if (in.storageLimits.known) {
        chargeW = std::min(chargeW, in.storageLimits.chargeW);
        dischargeW = std::min(dischargeW, in.storageLimits.dischargeW);
    }
    if (!pvChargeReachable(c, in)) { chargeW = 0; }
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
    return {chargeW, dischargeW};
}
struct EnergyLimits { uint32_t chargeWh, dischargeWh; };
EnergyLimits actionLimits(Config const& c, Policies::StorageEconomicGoals const& goals, Inputs const& in, Step const& slot, size_t index)
{
    auto power = powerLimits(c, in, slot, index);
    int64_t seconds = slot.end - slot.start;
    int64_t netWs = (static_cast<int64_t>(slot.loadW) - slot.pvW) * seconds;
    int64_t chargeWs = power.chargeW * seconds, dischargeWs = power.dischargeW * seconds;
    if (!goals.gridChargingAllowed) {
        int64_t surplus = std::max<int64_t>(0, -netWs);
        if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::ExportFirst && c.flow.pvExport) {
            surplus = std::max<int64_t>(0, surplus - c.flow.maxExportW * seconds);
        }
        chargeWs = std::min(chargeWs, surplus);
    }
    if (!goals.dischargeAllowed) { dischargeWs = 0; }
    if (!batteryExportAllowed(c)) { dischargeWs = std::min(dischargeWs, std::max<int64_t>(0, netWs)); }
    if (c.flow.enabled) { dischargeWs = std::min(dischargeWs, (slot.loadW + static_cast<int64_t>(c.flow.maxExportW)) * seconds); }
    // Match transition's ceil on charge and floor on discharge exactly. A zero
    // power/permission limit must never admit movement through integer rounding.
    return {static_cast<uint32_t>(chargeWs * c.chargePermille / 3600000),
        dischargeWs ? static_cast<uint32_t>(((dischargeWs + 1) * 1000 - 1) / (3600 * c.dischargePermille)) : 0};
}
bool transition(Config const& c, Policies::StorageEconomicGoals const& goals, Inputs const& in, Step const& slot, size_t index, uint32_t from, uint32_t to, Flow& flow)
{
    uint32_t seconds = slot.end - slot.start;
    int64_t deltaWs = (static_cast<int64_t>(to) - from) * 3600;
    int64_t netWs = (static_cast<int64_t>(slot.loadW) - slot.pvW) * seconds;
    auto power = powerLimits(c, in, slot, index);
    auto chargeW = power.chargeW, dischargeW = power.dischargeW;
    if (deltaWs < 0 && !goals.dischargeAllowed) { return false; }
    if (deltaWs > 0) {
        flow.batteryWs = (deltaWs * 1000 + c.chargePermille - 1) / c.chargePermille;
        // Grid charging means intentional additional import for the battery, after PV covers load.
        if (!goals.gridChargingAllowed && flow.batteryWs > std::max(static_cast<int64_t>(0), -netWs)) { return false; }
        if (flow.batteryWs > static_cast<int64_t>(chargeW) * seconds) { return false; }
    } else if (deltaWs < 0) {
        flow.batteryWs = -((-deltaWs * c.dischargePermille) / 1000);
        if (-flow.batteryWs > static_cast<int64_t>(dischargeW) * seconds) { return false; }
        // Disabled coordination retains the legacy compensation-based permission.
        if (!batteryExportAllowed(c) && -flow.batteryWs > std::max(static_cast<int64_t>(0), netWs)) { return false; }
    }
    // Export-first reserves the permitted surplus for export before charging.
    if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::ExportFirst && c.flow.pvExport
            && deltaWs > 0 && !goals.gridChargingAllowed
            && flow.batteryWs > std::max<int64_t>(0, -netWs - static_cast<int64_t>(c.flow.maxExportW) * seconds)) { return false; }
    if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::LocalFirst && netWs < 0 && chargeW && from < goals.ceilingWh) {
        // Allocate eligible PV before export/curtailment. Admission must depend
        // on the next physical whole Wh, not the spacing of the value samples.
        if (deltaWs < 0) { return false; }
        uint32_t next = std::min(goals.ceilingWh, to + 1);
        int64_t nextAc = (static_cast<int64_t>(next - from) * 3600 * 1000 + c.chargePermille - 1) / c.chargePermille;
        if (next > to && nextAc <= std::min(-netWs, static_cast<int64_t>(chargeW) * seconds)) { return false; }
    }
    int64_t grid = netWs + flow.batteryWs;
    if (c.flow.enabled) {
        int64_t permittedExport = static_cast<int64_t>(c.flow.maxExportW) * seconds;
        // No battery export can be corrected by curtailing PV after PV reaches zero.
        if (flow.batteryWs < -static_cast<int64_t>(slot.loadW) * seconds - permittedExport) { return false; }
        if (!c.flow.pvExport) { permittedExport = c.flow.batteryExport && deltaWs < 0 ? permittedExport : 0; }
        flow.curtailedWs = std::min<int64_t>(static_cast<int64_t>(slot.pvW) * seconds, std::max<int64_t>(0, -grid - permittedExport));
        grid += flow.curtailedWs;
        if (grid < -permittedExport) { return false; }
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
// Exact min-plus DP. An action cost is periodic-affine in whole Wh within each
// import/export/curtailment regime, including the existing Ws/money rounding.
// Charge period = eta_c/gcd(eta_c,1000); discharge period = 1000/gcd(eta_d,1000).
// Every action is evaluated when building runs: no interpolation or convexity
// assumption is used. Each verified affine run is convolved by a sliding minimum.
struct Row { int64_t* cost = nullptr; uint32_t* throughput = nullptr; uint32_t lo = 0, hi = 0; };
struct FreeStorage { void operator()(void* p) const noexcept { std::free(p); } };
struct Segment { uint16_t a, b; uint32_t start, end; };
struct Workspace {
    std::unique_ptr<void, FreeStorage> storage;
    Row rows[3];
    uint16_t* queue = nullptr;
    EnergyLimits limits[MaxSteps] {};
    uint32_t prefixCharge[MaxSteps+1] {}, prefixDischarge[MaxSteps+1] {};
    Segment pending[10] {};
    uint32_t lower = 0, count = 0, work = 0;
    int32_t priceScratch[MaxSteps] {};
    char const* error = nullptr;
    bool spend(uint32_t units = 1) {
        if (units > MaxWorkUnits - work) { error = "optimizer_work_limit"; return false; }
        work += units; return true;
    }
    bool init(uint32_t n) {
        count = n;
        // Three cost/throughput rows and one reusable deque. No parents indexed
        // by time: depth-first Hirschberg reconstruction needs only ten frames.
        storage.reset(Memory::allocate(38 * n));
        if (!storage) { error = "insufficient_memory"; return false; }
        auto costs = static_cast<int64_t*>(storage.get());
        auto through = reinterpret_cast<uint32_t*>(costs + 3*n);
        for (unsigned r = 0; r < 3; ++r) { rows[r].cost = costs + r*n; rows[r].throughput = through + r*n; }
        queue = reinterpret_cast<uint16_t*>(through + 3*n);
        return true;
    }
};
static_assert(MaxSteps*(MaxStates-1) < UINT32_MAX, "Throughput rows fit uint32_t");
static_assert(sizeof(Workspace) <= 8192, "Bound planner metadata in PSRAM");
bool clear(Workspace& w, Row& row) {
    if (!w.spend(w.count)) { return false; }
    std::fill(row.cost, row.cost+w.count, Infinity); return true;
}
void relax(Row& row, uint32_t index, int64_t cost, uint32_t through) {
    if (cost < row.cost[index] || (cost == row.cost[index] && through < row.throughput[index])) {
        row.cost[index] = cost; row.throughput[index] = through;
    }
}
struct ExactDp {
    Config const& c; Policies::StorageEconomicGoals const& goals; Inputs const& in; Plan& plan; Workspace& w;
    int targetBoundary = -1;
    // Convolve ONE verified arithmetic progression of actions, inclusive ends.
    bool convolve(Row const& src, Row& dst, int a, int b, unsigned period, int64_t base, int64_t slope) {
        int sign = a < 0 ? -1 : 1;
        for (unsigned g = 0; g < period; ++g) {
            int j = dst.lo + (g + period - dst.lo%period)%period;
            if (j > int(dst.hi)) { continue; }
            unsigned h = (g + period - (a%int(period)+int(period))%int(period))%period;
            int add = std::max<int>(src.lo, j-b);
            add += (h + period - add%int(period))%period;
            unsigned head = 0, tail = 0;
            auto keyCost = [&](uint32_t i) { return src.cost[i] - int64_t((i-h)/period)*slope; };
            auto keyThrough = [&](uint32_t i) { return int64_t(src.throughput[i]) - int64_t(sign)*i; };
            for (; j <= int(dst.hi); j += period) {
                if (!w.spend()) { return false; }
                while (add <= int(src.hi) && add <= j-a) {
                    if (!w.spend()) { return false; }
                    if (src.cost[add] != Infinity) {
                        auto cost = keyCost(add), through = keyThrough(add);
                        while (tail > head) {
                            auto old = w.queue[tail-1];
                            if (keyCost(old) < cost || (keyCost(old) == cost && keyThrough(old) < through)) { break; }
                            --tail;
                        }
                        w.queue[tail++] = add;
                    }
                    add += period;
                }
                while (head < tail && w.queue[head] < j-b) { ++head; }
                if (head < tail) {
                    auto from = w.queue[head];
                    int64_t cost = src.cost[from] + base + int64_t((j-int(from)-a)/int(period))*slope;
                    relax(dst, j, cost, src.throughput[from] + std::abs(j-int(from)));
                }
            }
        }
        return true;
    }
    bool advance(Row const& src, Row& dst, size_t t, bool reverse) {
        if (!clear(w,dst)) { return false; }
        auto const& slot = plan.steps[t];
        auto limits = w.limits[t];
        // A translated state removes SoC saturation from the action kernel.
        // The only non-translation-invariant Local First edges are handled
        // explicitly at the real ceiling below, using the same feasibility rule.
        auto kernelGoals = goals; kernelGoals.ceilingWh = UINT32_MAX;
        uint32_t anchor = w.count;
        auto action = [&](int delta) {
            if (!w.spend()) { return Infinity; }
            Flow f;
            return transition(c,kernelGoals,in,slot,t,anchor,anchor+delta,f) ? f.cost : Infinity;
        };
        auto apply = [&](int a, int b, unsigned period, int64_t cost, int64_t slope) {
            return reverse ? convolve(src,dst,-b,-a,period,cost+int64_t((b-a)/int(period))*slope,-slope)
                : convolve(src,dst,a,b,period,cost,slope);
        };
        auto hold = action(0);
        if (w.error) { return false; }
        if (hold != Infinity && !apply(0,0,1,hold,0)) { return false; }
        for (bool charging : {true,false}) {
            unsigned period = charging ? c.chargePermille/std::gcd<unsigned>(c.chargePermille,1000)
                : 1000/std::gcd<unsigned>(c.dischargePermille,1000);
            int limit = std::min<uint32_t>(w.count-1,charging ? limits.chargeWh : limits.dischargeWh);
            for (unsigned r = 1; r <= period && r <= unsigned(limit); ++r) {
                int delta = charging ? int(r) : -limit + (limit-int(r))%int(period);
                int end = charging ? limit : -1;
                while (delta <= end) {
                    auto cost = action(delta);
                    if (w.error) { return false; }
                    if (cost == Infinity) { delta += period; continue; }
                    int first = delta, last = delta;
                    int64_t slope = 0;
                    if (delta+int(period) <= end) {
                        auto next = action(delta+period);
                        if (w.error) { return false; }
                        if (next != Infinity) {
                            slope = next-cost; last += period;
                            int64_t previous = next;
                            while (last+int(period) <= end) {
                                auto value = action(last+period);
                                if (w.error) { return false; }
                                if (value == Infinity || value-previous != slope) { break; }
                                previous = value; last += period;
                            }
                        }
                    }
                    if (!apply(first,last,period,cost,slope)) { return false; }
                    delta = last+period;
                }
            }
        }
        if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::LocalFirst
                && slot.pvW > int64_t(slot.loadW)) {
            auto edge = [&](uint32_t from, uint32_t to) {
                if (!w.spend()) { return false; }
                uint32_t source = (reverse ? to : from)-w.lower, dest = (reverse ? from : to)-w.lower;
                if (source < src.lo || source > src.hi || dest < dst.lo || dest > dst.hi || src.cost[source] == Infinity) { return true; }
                Flow f;
                if (transition(c,goals,in,slot,t,from,to,f)) { relax(dst,dest,src.cost[source]+f.cost,src.throughput[source]+std::abs(int64_t(to)-from)); }
                return true;
            };
            auto ceiling = goals.ceilingWh;
            if (ceiling >= w.lower && ceiling-w.lower < w.count) {
                uint32_t start = ceiling-std::min(ceiling-w.lower,limits.chargeWh);
                for (uint32_t from = start; from <= ceiling; ++from) if (!edge(from,ceiling)) { return false; }
                start = ceiling-std::min(ceiling-w.lower,limits.dischargeWh);
                for (uint32_t to = start; to <= ceiling; ++to) if (!edge(ceiling,to)) { return false; }
            }
        }
        return true;
    }
    int sweep(unsigned a, unsigned b, uint32_t start, unsigned current, unsigned spare, int otherBoundary = -1, uint32_t otherEnergy = 0) {
        auto& row = w.rows[current];
        if (!clear(w,row)) { return -1; }
        row.lo = row.hi = start-w.lower; row.cost[row.lo] = 0; row.throughput[row.lo] = 0;
        bool reverse = a>b;
        while (a != b) {
            unsigned t = reverse ? a-1 : a;
            unsigned boundary = reverse ? a-1 : a+1;
            auto const& previous = w.rows[current]; auto& next = w.rows[spare];
            auto limits = w.limits[t];
            next.lo = previous.lo - std::min(previous.lo,reverse ? limits.chargeWh : limits.dischargeWh);
            next.hi = previous.hi + std::min(w.count-1-previous.hi,reverse ? limits.dischargeWh : limits.chargeWh);
            if (otherBoundary >= 0) {
                unsigned lo = std::min<unsigned>(boundary,otherBoundary), hi = std::max<unsigned>(boundary,otherBoundary);
                uint32_t charge = w.prefixCharge[hi]-w.prefixCharge[lo], discharge = w.prefixDischarge[hi]-w.prefixDischarge[lo];
                uint32_t endpoint = otherEnergy-w.lower;
                next.lo = std::max(next.lo, endpoint-std::min(endpoint,reverse ? discharge : charge));
                next.hi = std::min(next.hi, endpoint+std::min(w.count-1-endpoint,reverse ? charge : discharge));
            }
            if (int(boundary) == targetBoundary) { next.lo = std::max(next.lo,goals.targetWh>w.lower ? goals.targetWh-w.lower : 0); }
            if (next.lo > next.hi) { if (!clear(w,next)) { return -1; } return spare; }
            if (!advance(previous,next,t,reverse)) { return -1; }
            std::swap(current,spare); a = boundary;
        }
        return current;
    }
    bool reconstruct(uint32_t start, uint32_t end) {
        unsigned pending = 1;
        w.pending[0] = {0,plan.count,start,end};
        while (pending) {
            auto segment = w.pending[--pending];
            if (segment.b-segment.a == 1) {
                auto& slot = plan.steps[segment.a]; Flow f;
                if (!transition(c,goals,in,slot,segment.a,segment.start,segment.end,f)) { return false; }
                slot.startWh = segment.start; slot.endWh = segment.end; applyFlow(slot,f); continue;
            }
            unsigned mid = (segment.a+segment.b)/2;
            int forward = sweep(segment.a,mid,segment.start,0,1,segment.b,segment.end);
            if (forward < 0) { return false; }
            int x = forward == 0 ? 1 : 0, y = 3-forward-x;
            int backward = sweep(segment.b,mid,segment.end,x,y,segment.a,segment.start);
            if (backward < 0) { return false; }
            auto const& f = w.rows[forward]; auto const& b = w.rows[backward];
            Score best; uint32_t selected = 0;
            for (uint32_t e = std::max(f.lo,b.lo); e <= std::min(f.hi,b.hi); ++e) {
                if (!w.spend()) { return false; }
                if (f.cost[e] == Infinity || b.cost[e] == Infinity) { continue; }
                Score candidate {f.cost[e]+b.cost[e],uint64_t(f.throughput[e])+b.throughput[e]};
                if (better(candidate,best)) { best = candidate; selected = e+w.lower; }
            }
            if (best.cost == Infinity) { return false; }
            if (pending+2 > std::size(w.pending)) { return false; }
            w.pending[pending++] = {uint16_t(mid),segment.b,selected,segment.end};
            w.pending[pending++] = {segment.a,uint16_t(mid),segment.start,selected};
        }
        return true;
    }
};
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
    out.initialWh = out.finalWh = 0; out.runtimeUs = out.workUnits = out.workspaceBytes = 0;
    out.baselineValid = true; out.negativeTerminalPrice = false; out.terminalPrice = out.terminalDemandWh = 0;
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
            uint32_t physicalLimit = (in.observedPvAcLimitKnown || in.observedPvAcLimitW) ? std::min(c.flow.pvAcLimitW, in.observedPvAcLimitW) : c.flow.pvAcLimitW;
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
#if defined(ESP32)
    // Full HEMS targets PSRAM. Never ask the compatibility-only INTERNAL heap
    // to absorb this table, even on a variant with enough free address space.
    if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) { return fail("optimizer_requires_psram"); }
#endif
    auto w = Memory::make<Workspace>();
    if (!w) { return fail("insufficient_memory"); }
    uint32_t lower = goals.reserveWh, upper = goals.ceilingWh;
    out.initialWh = std::llround(storage.socPct.value * c.capacityWh / 100.0);
    if (out.initialWh < lower || out.initialWh > upper) { return fail("storage_soc_outside_configured_bounds"); }
    // Ignore inventory unreachable from the measured start within this horizon.
    // This also keeps future values useful for small actions on large batteries:
    // a 1 MWh nameplate must not dilute a 100 Wh reachable economic opportunity.
    uint64_t chargeReach = 0, dischargeReach = 0;
    for (size_t t = 0; t < out.count; ++t) {
        auto limits = actionLimits(c, goals, in, out.steps[t], t);
        chargeReach += limits.chargeWh; dischargeReach += limits.dischargeWh;
    }
    uint32_t sampleLower = out.initialWh - std::min<uint64_t>(out.initialWh - lower, dischargeReach);
    uint32_t sampleUpper = out.initialWh + std::min<uint64_t>(upper - out.initialWh, chargeReach);
    if (targetCovered && goals.targetWh > sampleUpper) { out.targetState = "target_unreachable"; return fail("target_unreachable"); }
    // A bounded continuation table replaces the moving start-energy floor.
    // Credible covered future tariffs cap inventory at its future refill value.
    // Missing tail tariffs use a transparent robust covered-price assumption,
    // independent of the current SoC. Price scratch is fixed and bounded.
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
    out.negativeTerminalPrice = !c.terminalPriceConfigured && assumedPrice < 0;
    out.terminalPrice = c.terminalPriceConfigured ? c.terminalPriceMicros : std::max(0, assumedPrice);
    out.terminalDemandWh = std::min<uint64_t>(upper - lower,
        (loadWs / std::max<uint32_t>(1, duration)) * c.terminalHours * 1000 / c.dischargePermille);
    // With nonnegative import/export prices and optional economic charging,
    // stock beyond all possible future discharge, terminal demand and target
    // has no value. Removing such extra charge cannot increase cost and reduces
    // throughput. Keep measured stock; do not apply this dominance rule to
    // negative prices or mandatory Local First/Export First semantics.
    bool nonnegative = !c.exportConfigured || c.exportMicros >= 0;
    for (size_t t = 0; t < out.count; ++t) { nonnegative = nonnegative && out.steps[t].usedPrice >= 0; }
    if (nonnegative && (!c.flow.enabled || c.flow.preference == Hems::Flow::Preference::Economic)) {
        uint64_t useful = uint64_t(lower)+dischargeReach+(out.terminalPrice ? out.terminalDemandWh : 0);
        useful = std::max<uint64_t>(useful,std::max(out.initialWh,targetCovered ? goals.targetWh : 0));
        sampleUpper = std::min<uint64_t>(sampleUpper,useful);
    }
    if (sampleUpper-sampleLower >= MaxStates) { return fail("optimizer_state_capacity_exceeded"); }
    out.states = sampleUpper-sampleLower+1;
    w->lower = sampleLower;
    if (!w->init(out.states)) { return fail(w->error); }
    out.workspaceBytes = sizeof(Workspace)+38*out.states;
    ExactDp dp {c,goals,in,out,*w};
    for (size_t t = 0; t < out.count; ++t) {
        w->limits[t] = actionLimits(c,goals,in,out.steps[t],t);
        w->prefixCharge[t+1] = w->prefixCharge[t]+w->limits[t].chargeWh;
        w->prefixDischarge[t+1] = w->prefixDischarge[t]+w->limits[t].dischargeWh;
        if (targetCovered && out.steps[t].end == goals.deadline) { dp.targetBoundary = t+1; }
    }
    int result = dp.sweep(0,out.count,out.initialWh,0,1);
    if (result < 0) { out.workUnits = w->work; return fail(w->error); }
    Score best; uint32_t selected = out.initialWh;
    auto const& row = w->rows[result];
    for (uint32_t e = row.lo; e <= row.hi; ++e) {
        if (row.cost[e] == Infinity) { continue; }
        Score candidate {row.cost[e]+2*terminalValue(c,out,e+sampleLower),row.throughput[e]};
        if (better(candidate,best)) { best = candidate; selected = e+sampleLower; }
    }
    if (best.cost == Infinity) {
        out.targetState = targetCovered ? "target_unreachable" : out.targetState;
        return fail(targetCovered ? "target_unreachable" : "no_feasible_plan");
    }
    if (!dp.reconstruct(out.initialWh,selected)) { out.workUnits = w->work; return fail(w->error ? w->error : "no_feasible_plan"); }
    out.workUnits = w->work;
    out.finalWh = selected;
    out.terminalNano = terminalValue(c,out,selected)-terminalValue(c,out,out.initialWh);
    int64_t actualCost = 0; uint64_t actualThroughput = 0;
    for (size_t t = 0; t < out.count; ++t) {
        auto const& slot = out.steps[t]; Flow actual, baseline;
        if (!transition(c,goals,in,slot,t,slot.startWh,slot.endWh,actual)) { return fail("optimizer_reconstruction_failed"); }
        actualCost += actual.cost; actualThroughput += std::abs(int64_t(slot.endWh)-slot.startWh);
        if (!transition(c,goals,in,slot,t,out.initialWh,out.initialWh,baseline)
                || (targetCovered && slot.end == goals.deadline && out.initialWh < goals.targetWh)) { out.baselineValid = false; }
        if (out.baselineValid) { out.baselineNano += baseline.cost/2; }
    }
    if (actualCost+2*terminalValue(c,out,selected) != best.cost || actualThroughput != best.throughput) { return fail("optimizer_reconstruction_failed"); }
    out.optimizedNano = actualCost/2+out.terminalNano;
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
    out["initial_wh"] = p.initialWh; out["final_wh"] = p.finalWh;
    out["generated_at"] = p.generatedAt; out["pv_included"] = p.pvIncluded; out["partial_horizon"] = p.partial;
    for (auto key : { "initial_soc_pct", "final_soc_pct", "baseline_cost_nano", "optimized_cost_nano", "modeled_savings_nano" }) { out[key] = nullptr; }
    if (p.count) {
        out["initial_soc_pct"] = 100.0 * p.initialWh / c.capacityWh; out["final_soc_pct"] = 100.0 * p.finalWh / c.capacityWh;
        out["optimized_cost_nano"] = p.optimizedNano;
        if (p.baselineValid) {
            out["baseline_cost_nano"] = p.baselineNano;
            out["modeled_savings_nano"] = p.baselineNano - p.optimizedNano;
        }
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
    out["baseline_policy"] = "hold_battery_under_same_flow_policy";
    out["baseline_valid"] = p.count && p.baselineValid;
    out["projected_grid_bill_difference_nano"] = nullptr;
    if (p.count && p.baselineValid) { out["projected_grid_bill_difference_nano"] = p.baselineNano - (p.importNano - p.exportNano); }
    uint32_t peakWh = p.initialWh;
    for (size_t n = 0; n < p.count; ++n) { peakWh = std::max(peakWh, p.steps[n].endWh); }
    out["charge_target_soc_pct"] = c.capacityWh ? static_cast<double>(peakWh) * 100 / c.capacityWh : 0;
    out["charge_target_reason"] = "load_pv_cost_and_constraints";
    out["load_forecast_source"] = p.learnedSlots ? "local_profile" : "constant_fallback";
    out["load_learned_slots"] = p.learnedSlots; out["load_total_slots"] = p.count;
    out["retained_by_hysteresis"] = p.retainedByHysteresis;
    out["soc_states"] = p.states; out["calculation_us"] = p.runtimeUs;
    out["calculation_work_units"] = p.workUnits; out["workspace_bytes"] = p.workspaceBytes;
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
    if (!batteryExportAllowed(c)) { restrictions.add("battery_export_forbidden"); }
    if (c.flow.enabled && !c.flow.pvExport) { restrictions.add("pv_export_forbidden"); }
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
    if (p.count && !p.baselineValid) { warnings.add("hold_baseline_infeasible"); }
    if (!pvChargeReachable(c, in)) { warnings.add("pv_charge_waiting_surplus"); }
    if (p.negativeTerminalPrice) { warnings.add("negative_terminal_headroom_unmodeled"); }
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
    limit = std::min(limit, static_cast<size_t>(96));
    out["schedule_offset"] = offset; out["has_more"] = offset + limit < p.count;
    for (size_t n = offset; n < p.count && n - offset < limit; ++n) {
        auto const& s = p.steps[n]; auto j = schedule.add<JsonObject>();
        j["start"] = s.start; j["end"] = s.end; j["market_price_micros_per_kwh"] = s.marketPrice;
        j["import_price_micros_per_kwh"] = nullptr; if (s.hasImportPrice) { j["import_price_micros_per_kwh"] = s.importPrice; }
        j["used_price_micros_per_kwh"] = s.usedPrice; j["pv_w"] = s.pvW;
        j["pv_wh"] = static_cast<double>(s.pvW) * (s.end - s.start) / 3600; j["assumed_load_w"] = s.loadW; j["load_coverage_pct"] = s.loadCoverage; j["load_learned"] = s.loadLearned;
        j["action"] = s.endWh > s.startWh ? "charge" : s.endWh < s.startWh ? "discharge" : "hold";
        j["battery_w"] = s.batteryW; j["start_wh"] = s.startWh; j["end_wh"] = s.endWh; j["start_soc_pct"] = 100.0 * s.startWh / c.capacityWh;
        j["end_soc_pct"] = 100.0 * s.endWh / c.capacityWh; j["grid_import_wh"] = static_cast<double>(s.gridImportWs) / 3600;
        j["grid_export_wh"] = static_cast<double>(s.gridExportWs) / 3600; j["cost_nano"] = s.costNano; j["grid_import_ws"] = s.gridImportWs; j["grid_export_ws"] = s.gridExportWs;
        j["reason"] = s.endWh == s.startWh ? "least_cost_hold_with_policies" : "horizon_cost_minimum_with_policies_and_losses";
    }
}
} // namespace Hems::Optimizer
