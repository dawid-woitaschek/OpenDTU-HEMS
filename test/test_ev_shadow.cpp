// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/EvPlanner.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
using namespace Hems;
int main()
{
    constexpr uint32_t now = 1792180800;
    auto tariff = std::make_unique<Energy::Series>(); auto pv = std::make_unique<Energy::Series>();
    auto storage = std::make_unique<Optimizer::Plan>(); auto result = std::make_unique<Optimizer::EvPlan>();
    Optimizer::Config c; c.effectivePrice = false; c.horizonSeconds = 7200; c.baseLoadW = 100;
    auto& ev = c.ev; ev.enabled = true; ev.deadline = now + 7200; ev.requiredWh = 2000;
    ev.voltage = 230; ev.phases = 1; ev.minCurrentA = 6; ev.maxCurrentA = 16;
    Optimizer::Inputs in; in.now = now; in.tariff = tariff.get(); in.forecast = pv.get();
    in.tariffStatus.fresh = in.tariffStatus.planningUsable = true;
    tariff->count = pv->count = 8;
    for (unsigned i = 0; i < 8; ++i) {
        tariff->intervals[i] = { now + i * 900, now + (i + 1) * 900, i < 4 ? 100000 : 400000 };
        pv->intervals[i] = { now + i * 900, now + (i + 1) * 900, i < 4 ? 0 : 4000 };
    }
    auto run = [&] { return Optimizer::calculateEv(c, in, *storage, *result); };
    auto reason = [&](char const* r) { assert(!strcmp(result->reason, r)); };
    auto invariant = [&] {
        int64_t ws = 0, cost = 0;
        for (unsigned i = 0; i < result->count; ++i) {
            auto const& s = result->steps[i];
            assert(s.start >= now && s.end <= ev.deadline && s.chargeEnd >= s.start && s.chargeEnd <= s.end);
            if (s.currentA) { assert(s.currentA >= ev.minCurrentA && s.currentA <= ev.maxCurrentA); }
            assert(s.storageW >= 0 || !s.currentA);
            ws += int64_t(s.currentA) * ev.voltage * ev.phases * (s.chargeEnd - s.start); cost += s.costNano;
        }
        assert(ws == result->plannedWs && cost == result->costNano);
        assert(result->plannedWs <= result->deliverableWs);
    };
    assert(run()); reason("planned"); invariant();
    assert(result->plannedWs >= 2000 * 3600 && result->plannedWs < 2000 * 3600 + 3680);
    assert(result->steps[2].chargeEnd < result->steps[2].end); // partial final interval
    auto serialized = [&](Optimizer::EvPlan const& plan) {
        JsonDocument j; Optimizer::serializeEv(c, in, plan, j.to<JsonObject>(), now, 4);
        std::string bytes; serializeJson(j, bytes); return bytes;
    };
    JsonDocument timing; Optimizer::serializeEv(c, in, *result, timing.to<JsonObject>(), now, 1);
    assert(timing["current_a"] == 16 && timing["next_change"] == now + 1957);
    assert(timing["next_action"] == "wait"); // contiguous equal-current slots are one action
    assert(result->costNano == 200048888); // 2000.488888... Wh at exactly 0.1 currency/kWh
    auto first = serialized(*result); assert(run()); assert(serialized(*result) == first);
    for (unsigned i = 0; i < 4; ++i) { tariff->intervals[i].value = -100000; }
    assert(run()); assert(result->costNano == -200048888);
    for (unsigned i = 0; i < 4; ++i) { tariff->intervals[i].value = 100000; }
    ev.deadline = now; assert(!run()); reason("deadline_passed");
    ev.deadline = now + 123; assert(run()); reason("energy_shortfall"); invariant(); assert(result->coverageEnd == ev.deadline);
    ev.deadline = now + 7200; ev.requiredWh = 0; assert(run()); reason("energy_not_required");
    ev.requiredWh = 1000000; assert(run()); reason("energy_shortfall"); invariant();
    ev.requiredWh = 2000; tariff->count = 1; assert(run()); reason("energy_shortfall");
    assert(result->coverageReason && result->coverageEnd == now + 900);
    tariff->count = 8; tariff->intervals[1].start += 300; assert(run()); assert(result->count == 1);
    tariff->intervals[1].start -= 300;
    in.tariffStatus.fresh = false; assert(!run()); reason("tariff_not_usable"); in.tariffStatus.fresh = true;
    in.forecastStatus.fresh = in.forecastStatus.planningUsable = true;
    assert(run()); invariant(); assert(result->pvIncluded && result->steps[4].currentA > 0 && !result->steps[0].currentA);
    assert(result->costNano == 0); // residual PV supplies all proposed charging
    pv->count = 2; assert(run()); assert(!result->pvIncluded); pv->count = 8;
    ev.maxPriceEnabled = true; ev.maxPriceMicros = 0;
    in.forecastStatus.fresh = false; assert(run()); reason("energy_shortfall"); assert(result->plannedWs == 0);
    in.forecastStatus.fresh = true; assert(run()); reason("planned"); invariant();
    // PV below minimum current cannot evade the grid-price cap.
    for (auto& interval : pv->intervals) { interval.value = 1400; }
    assert(run()); assert(result->plannedWs == 0); // 1300 W residual < 6 A * 230 V
    ev.maxPriceEnabled = false; ev.maxCurrentA = ev.minCurrentA = 6;
    ev.requiredWh = 1; assert(run()); invariant(); assert(result->steps[0].currentA == 6);
    assert(result->steps[0].chargeEnd - result->steps[0].start == 3);
    ev.minCurrentA = 7; assert(!run()); reason("invalid_configuration"); ev.minCurrentA = 6;
    ev.voltage = 0; assert(!run()); reason("electrical_model_missing"); ev.voltage = 230;
    // Existing storage projection is authoritative, and its discharge slots are excluded.
    c.enabled = true; assert(!run()); reason("storage_projection_unavailable");
    storage->count = 8; storage->pvIncluded = true;
    for (unsigned i = 0; i < 8; ++i) {
        storage->steps[i].start = now + i * 900; storage->steps[i].end = now + (i + 1) * 900;
        storage->steps[i].batteryW = i < 4 ? -500 : 500;
    }
    auto storageBefore = storage->steps[0].batteryW;
    ev.requiredWh = 2000; ev.maxCurrentA = 16;
    assert(run()); invariant(); assert(result->storageConflict && result->storageIncluded);
    assert(result->steps[0].currentA == 0 && storage->steps[0].batteryW == storageBefore);
    // Deadline is an absolute UTC instant across the European fall-back transition.
    c.enabled = false; in.now = 1792888200; // 2026-10-25 00:30Z
    ev.deadline = in.now + 7200; tariff->count = 1; tariff->intervals[0] = {in.now, ev.deadline, 100000};
    assert(run()); assert(result->coverageEnd == ev.deadline);
    // Arbitrary provider boundaries remain bounded even when their union exceeds 256 slots.
    c.horizonSeconds = 172800; ev.deadline = in.now + 172800; tariff->count = 256;
    for (unsigned i = 0; i < 256; ++i) { tariff->intervals[i] = {in.now + i * 675, in.now + (i + 1) * 675, 100000}; }
    assert(run()); assert(result->count == Optimizer::MaxSteps);
    assert(!strcmp(result->coverageReason, "planning_slot_capacity_exceeded"));
    JsonDocument config; Optimizer::serializeEvConfig(ev, config.to<JsonObject>()); Optimizer::EvConfig restored;
    assert(Optimizer::parseEvConfig(config.as<JsonVariantConst>(), restored) && restored.deadline == ev.deadline);
    config["phases"] = 4; assert(!Optimizer::parseEvConfig(config.as<JsonVariantConst>(), restored));
    std::cout << "EV shadow UTC/deadline, coverage/gaps/stale, PV/price/min-current, residual storage, cost and deterministic bounded schedule passed; EvPlan bytes=" << sizeof(Optimizer::EvPlan) << '\n';
}
