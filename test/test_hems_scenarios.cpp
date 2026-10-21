// SPDX-License-Identifier: GPL-2.0-or-later
#include "scenario/Harness.h"
using namespace Hems;
using Actuation::Action;
using Actuation::Origin;
int main(int argc, char** argv)
{
    assert(argc >= 2); std::string name = argv[1];
    Scenario::Harness h;
    auto protectedNow = [] { return !HemsController.getConstraints().storage.dischargeAllowed; };
    auto protectionStop = [&] { return h.commands(Action::Stop, Origin::Protection); };
    auto protectionRelease = [&] { return h.commands(Action::Release, Origin::Protection); };
    if (name.rfind("protection-", 0) == 0) {
        h.mode(Mode::Active); h.evCharging(true);
        bool intervention = name != "protection-charge" && name != "protection-idle";
        h.storagePower(name == "protection-charge" ? 500 : name == "protection-idle" ? 0 : -500);
        if (intervention) {
            for (unsigned i = 0; i < 12 && !protectionStop(); ++i) { h.advance(1000); }
            h.storagePower(0); // Script the observed stop; writes do not emulate physical power.
        }
        h.advance(6000); assert(protectedNow()); assert(protectionStop() == (intervention ? 1u : 0u));
        assert(HemsController.getControlMode(ControlTarget::Evse) == ControlMode::MonitorOnly);
        assert(!h.manual(Action::SetDischargePowerW, 500)); h.storagePower(0); h.advance(6000);
        h.evCharging(false); h.advance(6000);
        if (name == "protection-resume") { h.advance(90000); h.evCharging(true); h.advance(6000); assert(protectedNow()); h.evCharging(false); h.advance(6000); }
        if (name == "protection-stale") {
            h.advance(60000); h.advance(100000, false); // no worker refresh; stale cannot complete debounce
            assert(protectedNow()); h.advance(6000); assert(protectedNow());
        }
        if (name == "protection-unknown") {
            h.advance(60000); h.evUnknown(); h.advance(70000); assert(protectedNow()); h.evCharging(false); h.advance(6000);
        }
        if (name == "protection-reconnect") {
            h.advance(60000); h.connect(false); h.advance(130000); assert(protectedNow());
            h.connect(true); h.advance(6000); assert(protectedNow());
        }
        if (name == "protection-manual") { assert(h.manual(Action::SetChargePowerW, 500)); h.storagePower(500); h.advance(6000); }
        h.advance(100000); assert(protectedNow() && !protectionRelease());
        h.advance(25000); assert(!protectedNow());
        assert(protectionRelease() == (intervention && name != "protection-manual" ? 1u : 0u));
    } else if (name == "command-failures") {
        for (auto failure : {1, 2, 3, 0}) {
            faultReadbackAddress = 42010;
            writeFails = failure == 1; readbackFails = failure == 2; readbackMismatch = failure == 3;
            assert(h.manual(Action::Stop));
            auto result = HemsController.getActuationCommandResult(ControlTarget::Storage);
            assert(result.execution.error == (failure == 1 ? Actuation::ExecutionError::WriteFailed : failure == 2
                ? Actuation::ExecutionError::ReadbackFailed : failure == 3 ? Actuation::ExecutionError::VerificationFailed : Actuation::ExecutionError::None));
            writeFails = readbackFails = readbackMismatch = false; h.advance(6000);
        }
        h.storage.Control = ControlMode::MonitorOnly; Storage::StorageSettings.set(h.storage); HemsController.updateSettings(); h.advance(6000);
        auto cleanup = HemsController.getActuationCleanupSnapshot(ControlTarget::Storage);
        assert(cleanup.attempts == 1); auto count = writes.size(); h.advance(60000); assert(writes.size() == count);
    } else if (name == "shadow-no-evse") {
        h.planning(); h.mode(Mode::Active);
        h.evse.Control = ControlMode::HemsControl; Evse::BindingSettings.set(h.evse); HemsController.updateSettings();
        auto ev = h.economic.ev; ev.enabled = true; ev.voltage = 230; ev.phases = 3;
        ev.requiredWh = 5000; ev.deadline = scenarioUtc + 7200;
        assert(Optimizer::ShadowOptimizer.configureEv(ev)); h.advance(8000);
        JsonDocument report; Optimizer::ShadowOptimizer.serialize(report.to<JsonObject>(), true);
        assert(report["ev_shadow"]["shadow_only"] == true && report["ev_shadow"]["planned_wh"].as<double>() > 0);
        auto revision = report["plan_revision"].as<uint32_t>(); h.advance(20000);
        report.clear(); Optimizer::ShadowOptimizer.serialize(report.to<JsonObject>(), true);
        assert(report["plan_revision"] == revision);
        // Restart restores intent/config only. No schedule or ownership is persisted.
        Optimizer::Service restart; restart.init(); report.clear(); restart.serialize(report.to<JsonObject>(), true);
        assert(report["config"]["ev_shadow"]["enabled"] == true && report["ev_shadow"]["schedule"].size() == 0);
        assert(report["config"]["dispatch_enabled"] == false);
        for (auto const& w : writes) { assert(w.host != "warp.invalid"); }
        assert(writes.empty());
    } else if (name == "economic-gates") {
        h.planning(); h.mode(Mode::Active); h.advance(8000); assert(writes.empty());
        h.economic.dispatchEnabled = true; h.mode(Mode::Monitor); assert(Optimizer::ShadowOptimizer.configure(h.economic)); h.advance(8000); assert(writes.empty());
        h.storage.Control = ControlMode::MonitorOnly; Storage::StorageSettings.set(h.storage); h.mode(Mode::Active); h.advance(8000); assert(writes.empty());
        h.storage.Control = ControlMode::HemsControl; Storage::StorageSettings.set(h.storage); HemsController.updateSettings(); h.advance(8000);
        assert(h.commands(Action::SetChargePowerW, Origin::Economic) > 0);
        auto count = writes.size(); h.advance(20000); assert(writes.size() == count);
        assert(h.manual(Action::Stop)); h.advance(6000); assert(Optimizer::ShadowOptimizer.dispatchPlan().paused);
        Optimizer::Service restart; restart.init(); assert(restart.dispatchPlan().paused);
        Optimizer::ShadowOptimizer.init(); // Restore the shared worker callback after the reboot probe.
        count = writes.size(); h.advance(30000); assert(writes.size() == count);
        assert(Optimizer::ShadowOptimizer.resume()); assert(!Optimizer::ShadowOptimizer.dispatchPlan().valid); h.advance(8000);
        assert(Optimizer::ShadowOptimizer.dispatchPlan().valid && writes.size() > count);
        h.connect(false); h.advance(30000); count = writes.size(); h.advance(30000); assert(writes.size() == count);
        h.connect(true); h.advance(8000); count = writes.size(); h.advance(20000); assert(writes.size() == count);
    } else if (name == "economic-retry") {
        h.planning(true); writeFails = true; h.mode(Mode::Active); h.advance(15000);
        auto count = writes.size(); assert(count > 0); h.advance(10000); assert(writes.size() == count);
        h.advance(160000); auto stable = writes.size(); h.advance(60000); assert(writes.size() == stable);
        assert(h.commands(Action::SetChargePowerW, Origin::Economic) == 0);
    } else { assert(false && "unknown scenario"); }
    for (auto row : h.log.as<JsonArrayConst>()) {
        assert(row["target"] != "evse"); // no scenario requests any EVSE command
    }
    if (argc > 2) { std::ofstream file(argv[2]); serializeJsonPretty(h.log, file); }
    std::cout << name << ": passed (" << h.log.size() << " semantic observations)\n";
}
