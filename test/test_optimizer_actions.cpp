// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Planner.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
using namespace Hems;
using namespace Hems::Optimizer;
namespace {
constexpr uint32_t Now = 1792195200;
struct Fixture {
    Config c;
    Inputs in;
    Energy::Series tariff, pv;
    Plan p;
    Fixture() {
        c.enabled = c.socBoundsConfigured = true;
        c.capacityWh = 10000; c.minSoc = 20; c.maxSoc = 90;
        c.maxChargeW = c.maxDischargeW = 100;
        c.chargePermille = c.dischargePermille = 1000;
        c.terminalPriceConfigured = true; c.terminalPriceMicros = 0;
        c.horizonSeconds = 900;
        c.flow.enabled = true; c.flow.pvAcLimitW = 3000; c.wearSource = 2;
        tariff.count = pv.count = 192;
        for (unsigned n = 0; n < tariff.count; ++n) {
            tariff.intervals[n] = {Now + 900*n, Now + 900*(n+1), -100000, -100000, true};
            pv.intervals[n] = {Now + 900*n, Now + 900*(n+1), 0, 0, false};
        }
        in.now = Now; in.tariff = &tariff; in.forecast = &pv;
        in.tariffStatus.fresh = in.tariffStatus.planningUsable = true;
        in.forecastStatus = in.tariffStatus;
        in.storage.device.enabled = in.storage.device.reachable = in.storage.device.hasUpdate = true;
        in.storage.socPct = Model::Signal<double>::fresh(50.13); // Off both old/new regular grids.
    }
    void run() {
        in.tariff = &tariff; in.forecast = &pv;
        assert(calculate(c, in, p));
        assert(p.count && p.states <= MaxStates);
        uint32_t wh = p.initialWh;
        for (size_t t = 0; t < p.count; ++t) {
            auto const& s = p.steps[t];
            assert(s.startWh == wh && s.endWh >= c.capacityWh*c.minSoc/100 && s.endWh <= c.capacityWh*c.maxSoc/100);
            assert(s.batteryW <= int32_t(c.maxChargeW) && s.batteryW >= -int32_t(c.maxDischargeW));
            wh = s.endWh;
        }
        assert(wh == p.finalWh);
        Plan replay;
        assert(evaluateRetained(c, in, p, p, replay));
        assert(replay.finalWh == p.finalWh && replay.optimizedNano == p.optimizedNano);
    }
};
void smallActions() {
    for (unsigned capacity : {10000u, 1000000u}) for (unsigned spacing : {2u, 5u, 10u}) {
        Fixture f; f.c.capacityWh = capacity; f.c.socStepPct = spacing;
        f.run(); assert(f.p.finalWh == f.p.initialWh + 25);
        f.c.maxChargeW = f.c.maxDischargeW = 2000;
        f.in.storageLimits = {true, 12, 12}; f.run(); assert(f.p.finalWh == f.p.initialWh + 3);
        f.in.storageLimits.known = false;
        f.in.currentConstraints.maxChargePower = {true, 4};
        f.run(); assert(f.p.finalWh == f.p.initialWh + 1);
        f.in.currentConstraints.chargeAllowed = false;
        f.run(); assert(f.p.finalWh == f.p.initialWh);
        f.in.currentConstraints.chargeAllowed = true;
        f.in.currentConstraints.maxChargePower = {};
        f.c.chargePermille = 950; f.c.maxChargeW = 100;
        f.run(); assert(f.p.finalWh == f.p.initialWh + 23 && f.p.steps[0].batteryW <= 100);
        f.c.policies.gridChargingAllowed = false;
        f.c.maxChargeW = 2000; f.c.baseLoadW = 100; f.pv.intervals[0].value = 200;
        f.c.terminalPriceMicros = 200000; f.in.storage.socPct.value = 20.001;
        f.run(); assert(f.p.finalWh == f.p.initialWh + 23 && f.p.steps[0].gridImportWs == 0);
        f.c.policies.gridChargingAllowed = true; f.pv.intervals[0].value = 0; f.c.terminalPriceMicros = 0; f.in.storage.socPct.value = 50.13;
        f.tariff.intervals[0].value = f.tariff.intervals[0].importPrice = 200000;
        f.c.baseLoadW = 100; f.c.dischargePermille = 950;
        f.run(); assert(f.p.initialWh - f.p.finalWh == 26 && f.p.steps[0].gridExportWs == 0);
        f.in.currentConstraints.maxDischargePower = {true, 4};
        f.run(); assert(f.p.initialWh - f.p.finalWh == 1);
        f.in.currentConstraints.dischargeAllowed = false;
        f.run(); assert(f.p.finalWh == f.p.initialWh);
    }
}
void repeatedAndTarget() {
    Fixture f; f.c.horizonSeconds = 8*900; f.run();
    assert(f.p.finalWh == f.p.initialWh + 8*25);
    f.c.flow.preference = Flow::Preference::LocalFirst;
    f.c.policies.gridChargingAllowed = false;
    f.c.degradationMicros = 10000000; // Mandatory physical allocation, despite wear.
    for (auto& pv : f.pv.intervals) pv.value = 100;
    f.run(); assert(f.p.finalWh == f.p.initialWh + 8*25);
    f = Fixture(); f.c.horizonSeconds = 8*900;
    // A global future-price decision, rather than a profitable single-slot move.
    f.c.capacityWh = 1000000; f.c.baseLoadW = 100;
    f.in.storage.socPct.value = 20;
    for (unsigned n = 0; n < 8; ++n) f.tariff.intervals[n].value = f.tariff.intervals[n].importPrice = n < 4 ? 50000 : 400000;
    f.run(); assert(f.p.steps[3].endWh == f.p.initialWh + 100 && f.p.finalWh == f.p.initialWh);
    // Exact in-horizon reachability edge falls inside a value cell and requires
    // repeated 25 Wh actions. A one-second deadline split must not disappear.
    f = Fixture(); f.c.capacityWh = 10000; f.in.storage.socPct.value = 20;
    f.c.policies.targetEnabled = true; f.c.policies.targetSoc = 21;
    f.c.policies.targetDeadline = Now + 3601; f.c.horizonSeconds = 4500;
    f.run(); assert(f.p.steps[3].endWh >= 2100 && f.p.steps[4].end == Now + 3601);
    f.c.maxChargeW = 99;
    assert(!calculate(f.c, f.in, f.p) && !std::strcmp(f.p.blocker, "target_unreachable"));
}
// Independent exhaustive whole-Wh oracle where every inventory fits the value
// budget. This checks multi-slot economics, rather than mirroring the search.
void exhaustiveSmallBattery() {
    constexpr int64_t Unreachable = std::numeric_limits<int64_t>::max()/4;
    for (unsigned scenario = 0; scenario < 16; ++scenario) {
        Fixture f; f.c.capacityWh = 100; f.c.minSoc = 10; f.c.maxSoc = 90;
        f.c.horizonSeconds = 3600; f.c.maxChargeW = f.c.maxDischargeW = 12;
        f.c.baseLoadW = 8; f.in.storage.socPct.value = 50;
        f.c.policies.gridChargingAllowed = scenario & 1;
        f.c.policies.dischargeAllowed = scenario & 2;
        f.c.degradationMicros = scenario & 4 ? 30000 : 0;
        std::array<int64_t, 101> previous, next;
        previous.fill(Unreachable); previous[50] = 0;
        for (unsigned t = 0; t < 4; ++t) {
            int32_t price = ((scenario + 3*t) % 5 == 0) ? -100000 : 50000 + 100000*t;
            f.tariff.intervals[t].value = f.tariff.intervals[t].importPrice = price;
            f.pv.intervals[t].value = (scenario & 8) && t < 2 ? 12 : 0;
            int64_t netWh = (int64_t(f.c.baseLoadW) - f.pv.intervals[t].value)/4;
            next.fill(Unreachable);
            for (int from = 10; from <= 90; ++from) if (previous[from] != Unreachable) {
                for (int to = std::max(10, from-3); to <= std::min(90, from+3); ++to) {
                    int delta = to-from;
                    if (delta > 0 && !f.c.policies.gridChargingAllowed && delta > std::max<int64_t>(0, -netWh)) continue;
                    if (delta < 0 && (!f.c.policies.dischargeAllowed || -delta > std::max<int64_t>(0, netWh))) continue;
                    int64_t cost = 2*std::max<int64_t>(0, netWh+delta)*price + std::abs(delta)*f.c.degradationMicros;
                    next[to] = std::min(next[to], previous[from]+cost);
                }
            }
            previous = next;
        }
        f.run(); assert(f.p.optimizedNano == *std::min_element(previous.begin(), previous.end())/2);
    }
}
void boundedHorizon() {
    Fixture f; f.c.capacityWh = 1000000; f.c.horizonSeconds = 172800; f.in.now += 899;
    // Continuous intervals split at their own ends and UTC quarter hours
    // create >256 requested slots, exercising the explicit capacity boundary.
    f.tariff.count = 192;
    for (unsigned n = 0; n < f.tariff.count; ++n) {
        f.tariff.intervals[n] = {Now + n*901, Now + (n+1)*901, -100000, -100000, true};
    }
    auto before = std::chrono::steady_clock::now(); f.run();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();
    assert(f.p.count == MaxSteps && f.p.partial && !std::strcmp(f.p.partialReason, "planning_slot_capacity_exceeded"));
    std::cout << "256-slot small-action DP: " << us << " us, states=" << f.p.states << '\n';
}
} // namespace
int main() {
    smallActions(); repeatedAndTarget(); exhaustiveSmallBattery(); boundedHorizon();
    std::cout << "Small physical actions, exact replay, limits, repeated/global decisions and target edges passed\n";
}
