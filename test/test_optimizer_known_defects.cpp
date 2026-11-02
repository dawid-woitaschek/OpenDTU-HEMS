// SPDX-License-Identifier: GPL-2.0-or-later
// Audit regressions plus desired-behavior assertions for unresolved defects.
// Fixed cases must PASS. Only unresolved final invariants may XFAIL; setup
// failures and unexpected passes remain hard failures.
#include <hems/flow/Coordinator.h>
#include <hems/optimizer/Planner.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Hems;
using namespace Hems::Optimizer;

namespace {
constexpr uint32_t Now = 1792195200; // Fixed UTC quarter-hour; no wall clock, I/O or devices.
constexpr uint32_t SlotSeconds = 900;

struct KnownDefect : std::runtime_error { using std::runtime_error::runtime_error; };
void require(bool condition, std::string const& detail)
{
    if (!condition) { throw std::runtime_error(detail); }
}
void expectFixed(bool condition, std::string const& detail)
{
    if (!condition) { throw KnownDefect(detail); }
}

struct Fixture {
    Config c;
    Inputs in;
    Energy::Series tariff, pv;
    Plan plan;

    Fixture()
    {
        c.enabled = c.socBoundsConfigured = true;
        c.capacityWh = 2000; c.minSoc = 20; c.maxSoc = 90; c.socStepPct = 5;
        c.chargePermille = c.dischargePermille = 1000; // Exact Wh/Ws witnesses, no rounding ambiguity.
        c.maxChargeW = c.maxDischargeW = 2000; c.horizonSeconds = SlotSeconds;
        c.terminalPriceConfigured = true; c.terminalPriceMicros = 0;
        c.flow.enabled = true; c.flow.pvAcLimitW = c.flow.maxExportW = 3000;
        c.wearSource = 2; // Explicit zero wear.
        tariff.count = pv.count = 16;
        tariff.generatedAt = pv.generatedAt = Now;
        std::strcpy(tariff.source, "known-defect fixture"); std::strcpy(tariff.currency, "EUR");
        std::strcpy(pv.source, "known-defect PV");
        for (unsigned n = 0; n < tariff.count; ++n) {
            tariff.intervals[n] = {Now + n * SlotSeconds, Now + (n + 1) * SlotSeconds, 200000, 200000, true};
            pv.intervals[n] = {Now + n * SlotSeconds, Now + (n + 1) * SlotSeconds, 0, 0, false};
        }
        in.now = Now; in.tariff = &tariff; in.forecast = &pv;
        in.tariffStatus.enabled = in.tariffStatus.hasData = in.tariffStatus.fresh = in.tariffStatus.planningUsable = true;
        in.forecastStatus = in.tariffStatus;
        in.storage.device.enabled = in.storage.device.reachable = in.storage.device.hasUpdate = true;
        in.storage.socPct = Model::Signal<double>::fresh(50);
    }
    bool run() { return calculate(c, in, plan); }
    void ready()
    {
        bool calculated = run();
        require(calculated, std::string("fixture not ready: ") + (plan.blocker ? plan.blocker : "unknown"));
        require(plan.count > 0, "ready fixture has no steps");
    }
};

// The production retained-plan evaluator accepts the physical 25 Wh action.
// The DP must be able to choose an alternative at least as good as that witness.
void smallAction(bool charge)
{
    Fixture f;
    f.c.capacityWh = 10000; // 5% lattice = 500 Wh, but 100 W * 15 min = 25 Wh.
    f.c.maxChargeW = f.c.maxDischargeW = 100;
    f.c.baseLoadW = charge ? 0 : 100;
    f.tariff.intervals[0].value = f.tariff.intervals[0].importPrice = charge ? -100000 : 200000;
    f.ready();
    auto witness = f.plan;
    witness.steps[0].endWh = charge ? witness.initialWh + 25 : witness.initialWh - 25;
    Plan evaluated;
    require(evaluateRetained(f.c, f.in, f.plan, witness, evaluated), "small physical action rejected by production evaluator");
    require(evaluated.steps[0].batteryW == (charge ? 100 : -100), "witness must use exactly the permitted power");
    require(evaluated.optimizedNano < f.plan.baselineNano, "physical action must improve on hold");
    require(f.plan.optimizedNano <= evaluated.optimizedNano,
        "DP cost=" + std::to_string(f.plan.optimizedNano) + ", feasible 25 Wh cost=" + std::to_string(evaluated.optimizedNano));
}
void smallCharge() { smallAction(true); }
void smallDischarge() { smallAction(false); }

void forbiddenPvExport()
{
    Fixture f;
    f.c.flow.pvExport = false; f.c.flow.batteryExport = true;
    f.c.exportConfigured = true; f.c.exportMicros = 400000;
    f.pv.intervals[0].value = 1000;
    f.ready();
    auto leaking = f.plan.steps[0];
    // With unity efficiency, all exported energy must be attributable to battery
    // discharge. PV must be consumed locally or curtailed, even during discharge.
    int64_t batteryExportWs = std::max<int64_t>(0, (int64_t(leaking.startWh) - leaking.endWh) * 3600);
    f.c.flow.batteryExport = false; f.ready();
    require(f.plan.steps[0].gridExportWs == 0, "no-export control must curtail surplus");
    expectFixed(leaking.gridExportWs <= batteryExportWs,
        "export Ws=" + std::to_string(leaking.gridExportWs) + ", battery supplies only=" + std::to_string(batteryExportWs));
}

void forbiddenTerminalUse()
{
    Fixture f;
    f.c.baseLoadW = 1000; f.c.policies.dischargeAllowed = false;
    f.c.terminalPriceMicros = 400000;
    f.tariff.intervals[0].value = f.tariff.intervals[0].importPrice = 50000;
    f.ready();
    auto valued = f.plan;
    f.c.terminalPriceMicros = 0; f.ready();
    require(f.plan.finalWh == f.plan.initialWh, "zero-value control must not buy unusable inventory");
    expectFixed(valued.terminalNano == 0 && valued.finalWh == f.plan.finalWh,
        "discharge forbidden but terminal credit=" + std::to_string(valued.terminalNano)
        + ", final Wh=" + std::to_string(valued.finalWh) + ", zero-value control Wh=" + std::to_string(f.plan.finalWh));
}

void equivalentTariffSegmentation()
{
    Fixture coarse, split;
    for (auto f : {&coarse, &split}) {
        f->c.baseLoadW = 400; f->c.terminalPriceConfigured = false; f->c.terminalHours = 1;
        f->c.policies.gridChargingAllowed = false;
        f->in.storage.socPct = Model::Signal<double>::fresh(30); // Unsaturated terminal inventory value.
        f->tariff.intervals[1] = {Now + 900, Now + 2700, 50000, 50000, true};
    }
    coarse.tariff.count = 3;
    coarse.tariff.intervals[2] = {Now + 2700, Now + 4500, 400000, 400000, true};
    split.tariff.count = 4;
    split.tariff.intervals[2] = {Now + 2700, Now + 3600, 400000, 400000, true};
    split.tariff.intervals[3] = {Now + 3600, Now + 4500, 400000, 400000, true};
    // Same pointwise prices and coverage, including the complete continuation.
    for (uint32_t t = Now; t < Now + 4500; ++t) {
        auto a = coarse.tariff.current(t), b = split.tariff.current(t);
        require(a && b && a->value == b->value && a->importPrice == b->importPrice, "tariffs are not equivalent");
    }
    coarse.ready(); split.ready();
    require(coarse.plan.terminalFutureCovered && split.plan.terminalFutureCovered, "continuation must be fully covered");
    require(coarse.plan.count == 1 && split.plan.count == 1, "only future segmentation may differ");
    expectFixed(coarse.plan.terminalPrice == split.plan.terminalPrice
            && coarse.plan.finalWh == split.plan.finalWh && coarse.plan.optimizedNano == split.plan.optimizedNano,
        "equivalent tariff terminal prices=" + std::to_string(coarse.plan.terminalPrice) + "/" + std::to_string(split.plan.terminalPrice)
        + ", final Wh=" + std::to_string(coarse.plan.finalWh) + "/" + std::to_string(split.plan.finalWh));
}

void localFirstMismatch()
{
    Fixture f;
    f.c.baseLoadW = 500; f.c.policies.gridChargingAllowed = false;
    f.pv.intervals[0].value = 2000; // 1500 W surplus = 375 Wh; ample battery headroom.
    Model::SystemState s;
    s.grid.powerW = Model::Signal<double>::fresh(-1500);
    s.pv.powerW = Model::Signal<double>::fresh(2000);
    s.storage = f.in.storage; s.storage.acPowerW = Model::Signal<double>::fresh(0);
    auto live = [&](int32_t requested) {
        return Flow::coordinate(f.c.flow, s, requested, f.c.maxChargeW, f.c.maxDischargeW,
            true, true, false, true, false);
    };
    f.ready();
    auto economic = live(f.plan.steps[0].batteryW);
    require(economic.usable && economic.batteryW == f.plan.steps[0].batteryW, "Economic control must agree");
    f.c.flow.preference = Flow::Preference::LocalFirst; f.ready();
    auto coordinated = live(f.plan.steps[0].batteryW);
    require(coordinated.usable && coordinated.exportEnforcement, "fresh measured flow and DPL must be usable");
    require(coordinated.batteryW == f.plan.steps[0].batteryW,
        "same Local First flow: planner W=" + std::to_string(f.plan.steps[0].batteryW)
        + ", coordinator W=" + std::to_string(coordinated.batteryW));
}

void futureTargetReachability()
{
    Fixture f;
    f.c.baseLoadW = 2000;
    f.c.policies.targetEnabled = true; f.c.policies.targetSoc = 70; f.c.policies.targetDeadline = Now + 1800;
    f.tariff.intervals[0].value = f.tariff.intervals[0].importPrice = 400000;
    f.tariff.intervals[1].value = f.tariff.intervals[1].importPrice = 50000;
    // A full-horizon control demonstrates feasibility with the same prices/policy.
    f.c.horizonSeconds = 1800; f.ready();
    require(f.plan.finalWh >= 1400 && !std::strcmp(f.plan.targetState, "target_planned"), "full target must be feasible");
    f.c.horizonSeconds = 900; f.ready();
    require(f.plan.partial && !std::strcmp(f.plan.targetState, "target_horizon_incomplete"), "deadline must be outside horizon");
    auto afterCurrentWh = f.plan.finalWh;
    f.in.now += 900;
    // Holding the original 1000 Wh leaves 500 Wh charge capacity before the deadline.
    f.ready();
    require(f.plan.finalWh >= 1400, "holding current inventory must leave target reachable");
    f.in.storage.socPct = Model::Signal<double>::fresh(100.0 * afterCurrentWh / f.c.capacityWh);
    bool reachable = f.run();
    require(reachable || (f.plan.blocker && !std::strcmp(f.plan.blocker, "target_unreachable")), "unexpected follow-up blocker");
    expectFixed(reachable, "current action leaves " + std::to_string(afterCurrentWh)
        + " Wh; remaining physical charge <=500 Wh cannot reach 1400 Wh");
}

void expiredFulfilledTarget()
{
    Fixture f;
    f.c.policies.targetEnabled = true; f.c.policies.targetSoc = 50; f.c.policies.targetDeadline = Now + 900;
    f.ready();
    require(f.plan.finalWh >= 1000 && !std::strcmp(f.plan.targetState, "target_planned"), "target must be fulfilled at deadline");
    f.in.storage.socPct = Model::Signal<double>::fresh(100.0 * f.plan.finalWh / f.c.capacityWh);
    f.in.now += 1800; // Strictly after the fulfilled deadline, still within fresh fixture coverage.
    bool usable = f.run();
    require(usable || (f.plan.blocker && !std::strcmp(f.plan.blocker, "target_deadline_passed")), "unexpected expired-target blocker");
    f.c.policies.targetEnabled = false; f.ready();
    expectFixed(usable, "fulfilled one-shot target blocks later planning with target_deadline_passed");
}

void incompletePvForecast()
{
    Fixture f;
    f.c.horizonSeconds = 1800; f.c.baseLoadW = 500;
    f.c.policies.gridChargingAllowed = false; f.pv.intervals[0].value = 1500;
    f.ready();
    require(f.plan.pvIncluded && f.plan.steps[0].pvW == 1500 && f.plan.steps[0].gridImportWs == 0,
        "complete forecast control must use covered PV");
    f.pv.count = 1; // Only the later slot is unknown; current 15 min remains valid.
    Energy::JsonProvider provider(Energy::Domain::Forecast);
    Energy::Settings settings; settings.enabled = true; settings.horizonSeconds = 1800;
    provider.configure(settings);
    require(provider.accept(f.pv, Now, 0), "partial forecast provider acceptance");
    f.in.forecast = &provider.data(); f.in.forecastStatus = provider.status(Now, 0);
    require(f.in.forecastStatus.fresh && f.in.forecastStatus.planningUsable && f.in.forecastStatus.partial,
        "partial forecast must still be fresh and planning-usable");
    int64_t knownWs = 0;
    require(provider.data().energyWattSeconds(Now, Now + 900, knownWs) && knownWs == 1500 * 900,
        "covered forecast energy must be intact");
    f.ready();
    expectFixed(f.plan.steps[0].pvW == 1500 && f.plan.steps[0].gridImportWs == 0,
        "later missing forecast discards valid current PV: planned W=" + std::to_string(f.plan.steps[0].pvW)
        + ", current import Ws=" + std::to_string(f.plan.steps[0].gridImportWs));
}
} // namespace

int main()
{
    struct Case { char const* id; void (*run)(); bool fixed = false; };
    Case const cases[] = {
        {"KD01-small-charge", smallCharge, true}, {"KD01-small-discharge", smallDischarge, true},
        {"KD02-forbidden-PV-export", forbiddenPvExport}, {"KD03-forbidden-terminal-use", forbiddenTerminalUse},
        {"KD04-tariff-segmentation", equivalentTariffSegmentation}, {"KD05-local-first-original-witness", localFirstMismatch, true},
        {"KD06-future-target-reachability", futureTargetReachability}, {"KD07-expired-fulfilled-target", expiredFulfilledTarget},
        {"KD08-incomplete-PV-forecast", incompletePvForecast},
    };
    unsigned passed = 0, reproduced = 0, failures = 0;
    for (auto const& test : cases) {
        try {
            test.run();
            if (test.fixed) { std::cout << "PASS " << test.id << '\n'; ++passed; }
            else {
                std::cerr << "XPASS " << test.id << ": review changed behavior and promote to a normal regression\n";
                ++failures;
            }
        } catch (KnownDefect const& e) {
            if (test.fixed) { std::cerr << "FAIL " << test.id << ": " << e.what() << '\n'; ++failures; }
            else { std::cout << "XFAIL " << test.id << ": " << e.what() << '\n'; ++reproduced; }
        } catch (std::exception const& e) {
            std::cerr << "FAIL " << test.id << ": " << e.what() << '\n';
            ++failures;
        }
    }
    std::cout << passed << " fixed regressions passed; " << reproduced
              << " unresolved invariant failures reproduced; " << failures << " failures/XPASS\n";
    return failures ? 1 : 0;
}
