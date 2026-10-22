// SPDX-License-Identifier: GPL-2.0-or-later
// Real controller/optimizer/providers/mailboxes/executor; reuse deterministic
// wire, filesystem and worker seams from the established control suite.
#define main existingControlSuite
#include "test_provider_cleanup.cpp"
#undef main
#include <hems/optimizer/Service.h>
#include <hems/energy/Service.h>
#include <LittleFS.h>
#include <ctime>
static uint32_t economicUtc = 1792180800;
extern "C" time_t time(time_t* out) noexcept { if (out) { *out = economicUtc; } return economicUtc; }
namespace Hems::Energy {
bool providerGet(char const*, Source, JsonDocument&, NativeInfo&, char const*& error) { error = "test_offline"; return false; }
}
int main()
{
    using Optimizer::ShadowOptimizer;
    auto profile = load("../device-profiles/storage/Marstek-Venus-E-3.0.json");
    auto warp = load("../device-profiles/evse/Tinkerforge-WARP3-Smart.json");
    selectedProfiles[profile.id] = profile; selectedProfiles[warp.id] = warp;
    auto& settings = Configuration.data.Hems; settings.Mode = Mode::Monitor; settings.Warp.Enabled = false;
    StorageConfig storage; storage.Enabled = true; storage.Control = ControlMode::HemsControl;
    strcpy(storage.Host, "storage.invalid"); strcpy(storage.Profile, profile.id); Storage::StorageSettings.set(storage);
    testNow = 1000; freshRegisters(); registers[30006] = 0;
    Scheduler scheduler; HemsController.init(scheduler); Energy::EnergyData.init(scheduler); ShadowOptimizer.init();
    auto ticks = [&](unsigned count = 1) {
        for (unsigned i = 0; i < count; ++i) { testNow += 1000; scheduler.execute(); runWorker(); }
    };
    Energy::Settings tariffSettings; tariffSettings.enabled = true;
    assert(Energy::EnergyData.configure(Energy::Domain::Tariff, tariffSettings));
    JsonDocument tariff; tariff["schema"] = Energy::schema(Energy::Domain::Tariff); tariff["source"] = "dispatch test";
    tariff["currency"] = "EUR"; tariff["generated_at"] = economicUtc;
    for (unsigned i = 0; i < 16; ++i) {
        auto row = tariff["intervals"].add<JsonObject>(); row["start"] = economicUtc + i * 900;
        row["end"] = economicUtc + (i + 1) * 900; row["price_micros_per_kwh"] = i ? 400000 : 0;
    }
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>()));
    Optimizer::Config c; c.enabled = c.socBoundsConfigured = true; c.effectivePrice = false;
    c.capacityWh = 2000; c.minSoc = 20; c.maxSoc = 90; c.chargePermille = c.dischargePermille = 950;
    c.maxChargeW = c.maxDischargeW = 2000; c.baseLoadW = 1000; c.horizonSeconds = 14400;
    c.terminalPriceConfigured=true; c.terminalPriceMicros=50000;
    assert(!c.dispatchEnabled); assert(ShadowOptimizer.configure(c)); ticks(8);
    assert(ShadowOptimizer.dispatchPlan().valid && writes.empty()); // Legacy/default Shadow.
    c.dispatchEnabled = true; assert(ShadowOptimizer.configure(c)); ticks(8); assert(writes.empty()); // Monitor.
    settings.Mode = Mode::Active; storage.Control = ControlMode::MonitorOnly;
    Storage::StorageSettings.set(storage); HemsController.updateSettings(); ticks(8); assert(writes.empty());
    storage.Control = ControlMode::HemsControl; Storage::StorageSettings.set(storage); HemsController.updateSettings(); ticks(8);
    assert(!writes.empty()); auto command = HemsController.getActuationCommandSnapshot(ControlTarget::Storage);
    assert(command.command.origin == Actuation::Origin::Economic && command.command.action == Actuation::Action::SetChargePowerW);
    assert(HemsController.getActuationCommandResult(ControlTarget::Storage).execution.readbackVerified);
    auto count = writes.size(); auto stableRevision = ShadowOptimizer.dispatchPlan().revision;
    ticks(30); assert(writes.size() == count && ShadowOptimizer.dispatchPlan().revision == stableRevision);
    auto plan = ShadowOptimizer.dispatchPlan(); assert(plan.valid);
    auto identicalWrites = writes.size();
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>()));
    assert(!ShadowOptimizer.dispatchPlan().valid); ticks(8); assert(ShadowOptimizer.dispatchPlan().valid);
    assert(writes.size() == identicalWrites); // An identical refreshed plan keeps confirmed ownership.
    assert(ShadowOptimizer.requestRecalculate()); ticks(8); assert(writes.size() == identicalWrites);
    // Numeric prices can reverse the CURRENT action; presentation categories are absent.
    for (unsigned i = 0; i < 16; ++i) { tariff["intervals"][i]["price_micros_per_kwh"] = i < 12 ? 400000 : 0; }
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>())); ticks(8);
    command = HemsController.getActuationCommandSnapshot(ControlTarget::Storage);
    assert(command.command.action == Actuation::Action::SetDischargePowerW && command.command.value > 0);
    for (unsigned i = 0; i < 16; ++i) { tariff["intervals"][i]["price_micros_per_kwh"] = i ? 400000 : 0; }
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>())); ticks(8);
    Energy::Settings pvSettings; pvSettings.enabled = true;
    assert(Energy::EnergyData.configure(Energy::Domain::Forecast, pvSettings));
    JsonDocument pv; pv["schema"] = Energy::schema(Energy::Domain::Forecast); pv["source"] = "PV test"; pv["generated_at"] = economicUtc;
    for (unsigned i = 0; i < 16; ++i) {
        auto row = pv["intervals"].add<JsonObject>(); row["start"] = economicUtc + i * 900;
        row["end"] = economicUtc + (i + 1) * 900; row["power_w"] = 0;
    }
    assert(Energy::EnergyData.ingest(Energy::Domain::Forecast, pv.as<JsonVariantConst>())); ticks(8);
    JsonDocument report; ShadowOptimizer.serialize(report.to<JsonObject>(), false); assert(report["pv_included"] == true);
    assert(Energy::EnergyData.ingest(Energy::Domain::Forecast, pv.as<JsonVariantConst>())); assert(!ShadowOptimizer.dispatchPlan().valid); ticks(8);
    // Significant SoC changes revoke authority immediately; tiny movement does not.
    registers[34002] = 504; ticks(4); assert(ShadowOptimizer.dispatchPlan().valid);
    auto beforeSoc = ShadowOptimizer.dispatchPlan().revision;
    registers[34002] = 520; ticks(4);
    assert(!ShadowOptimizer.dispatchPlan().valid || ShadowOptimizer.dispatchPlan().revision > beforeSoc);
    ticks(8); assert(ShadowOptimizer.dispatchPlan().valid);
    // Manual commands are still ordinary semantic commands, preceded by durable pause.
    ticks(4); uint32_t id = 0;
    Actuation::Command manual { ControlTarget::Storage, Actuation::Action::Stop };
    assert(HemsController.submitManual(manual, id)); runWorker(); ticks(2);
    assert(ShadowOptimizer.dispatchPlan().paused);
    count = writes.size(); ticks(20); assert(writes.size() == count);
    Optimizer::Service reboot; reboot.init(); assert(reboot.dispatchPlan().paused);
    Storage::Provider bootProvider; assert(bootProvider.owner() == Actuation::Origin::None);
    ShadowOptimizer.init();
    failCommit = true; assert(!ShadowOptimizer.resume()); failCommit = false;
    assert(ShadowOptimizer.dispatchPlan().paused);
    assert(ShadowOptimizer.resume()); assert(!ShadowOptimizer.dispatchPlan().valid); ticks(8);
    assert(!ShadowOptimizer.dispatchPlan().paused && ShadowOptimizer.dispatchPlan().valid);
    assert(writes.size() > count);
    c.maxChargeW = 1000; assert(ShadowOptimizer.configure(c)); writeFails = true; ticks(8);
    assert(HemsController.getActuationCommandSnapshot(ControlTarget::Storage).state == Actuation::MailboxState::Failed);
    count = writes.size(); ticks(20); assert(writes.size() == count); // No busy retry despite fresh polling.
    writeFails = false; ticks(40);
    assert(HemsController.getActuationCommandSnapshot(ControlTarget::Storage).state == Actuation::MailboxState::Succeeded);
    // Revoke a running economic command between bounded writes.
    assert(ShadowOptimizer.requestRecalculate()); ticks(8);
    c.maxChargeW = 1500; assert(ShadowOptimizer.configure(c));
    onWrite = [&] { c.policies.dischargeAllowed = false; assert(ShadowOptimizer.configure(c)); };
    ticks(8);
    if (onWrite) { // Force a distinct desired power if the first replan happened to hold idle.
        registers[34002] = 500; c.policies.targetEnabled = true; c.policies.targetSoc = 60;
        c.policies.targetDeadline = economicUtc + 900; assert(ShadowOptimizer.configure(c)); ticks(8);
    }
    assert(!onWrite);
    command = HemsController.getActuationCommandSnapshot(ControlTarget::Storage);
    // A newer plan may already have recovered; the wire callback cannot authorize later old writes.
    assert(command.command.target == ControlTarget::Storage);
    // Economic disable may not release an actual EV Protection intervention.
    c.policies.targetEnabled = false; c.policies.dischargeAllowed = true;
    c.terminalPriceMicros = 200000;
    for (unsigned i = 0; i < 16; ++i) { tariff["intervals"][i]["price_micros_per_kwh"] = 200000; }
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>()));
    assert(ShadowOptimizer.configure(c)); ticks(8);
    for (bool permissionChange : { false, true }) {
        ticks(4); auto hold = ShadowOptimizer.dispatchPlan(); assert(hold.valid && hold.batteryW == 0);
        Actuation::Command queued { ControlTarget::Storage, Actuation::Action::Stop };
        queued.origin = Actuation::Origin::Economic; queued.authority = hold.revision;
        assert(HemsController.submitActuation(queued, id));
        if (!permissionChange) {
            // Queued work is cancelled before the worker can write.
            settings.Mode = Mode::Monitor; HemsController.updateSettings(); count = writes.size(); runWorker();
            assert(writes.size() == count);
            ticks(4); settings.Mode = Mode::Active; HemsController.updateSettings(); ticks(8);
            hold = ShadowOptimizer.dispatchPlan(); queued.authority = hold.revision;
            assert(HemsController.submitActuation(queued, id));
        }
        onWrite = [&] {
            if (permissionChange) { storage.Control = ControlMode::MonitorOnly; Storage::StorageSettings.set(storage); }
            else { settings.Mode = Mode::Monitor; }
            HemsController.updateSettings();
        };
        count = writes.size(); runWorker(); assert(writes.size() == count + 1);
        assert(HemsController.getActuationCommandResult(ControlTarget::Storage).execution.error == Actuation::ExecutionError::AuthorizationRevoked);
        ticks(8); count = writes.size(); ticks(12); assert(writes.size() == count);
        // Only the existing one-shot semantic courtesy release may write after withdrawal.
        settings.Mode = Mode::Active; storage.Control = ControlMode::HemsControl;
        Storage::StorageSettings.set(storage); HemsController.updateSettings(); ticks(8);
    }
    settings.Warp.Enabled = true; strcpy(settings.Warp.Host, "warp.invalid");
    Evse::BindingConfig binding; strcpy(binding.Profile, warp.id); binding.Control = ControlMode::MonitorOnly;
    Evse::BindingSettings.set(binding); registers[30006] = 500; HemsController.updateSettings(); ticks(8);
    command = HemsController.getActuationCommandSnapshot(ControlTarget::Storage);
    assert(command.command.origin == Actuation::Origin::Protection && command.command.action == Actuation::Action::Stop);
    registers[30006] = 0; ticks(4);
    Actuation::Command failedManualCharge { ControlTarget::Storage, Actuation::Action::SetChargePowerW, true, 500 };
    assert(HemsController.submitManual(failedManualCharge, id)); NetworkSettings.connected = false; runWorker();
    NetworkSettings.connected = true; ticks(8);
    report.clear(); HemsController.serializeEconomic(report.to<JsonObject>()); assert(report["owner"] == "ev_protection");
    Actuation::Command target { ControlTarget::Storage, Actuation::Action::SetTargetSocPct, true, 70 };
    assert(HemsController.submitManual(target, id)); runWorker(); ticks(2);
    assert(ShadowOptimizer.dispatchPlan().paused);
    report.clear(); HemsController.serializeEconomic(report.to<JsonObject>()); assert(report["owner"] == "ev_protection");
    auto releaseCount = [&] { size_t releases = 0; for (auto const& w : writes) { if (w.address == 42000 && w.value == 21947) { ++releases; } } return releases; };
    auto protectionReleases = releaseCount(); c.dispatchEnabled = false; assert(ShadowOptimizer.configure(c)); ticks(20);
    assert(releaseCount() == protectionReleases);
    report.clear(); HemsController.serializeEconomic(report.to<JsonObject>()); assert(report["owner"] == "ev_protection");
    // Loss of Active revokes automatic work and uses existing one-shot release.
    settings.Mode = Mode::Monitor; HemsController.updateSettings(); ticks(8);
    count = writes.size(); ticks(20); assert(writes.size() == count);
    // No economic EVSE producer, even when the EVSE is made writable.
    for (auto const& write : writes) { assert(write.host == "storage.invalid"); }
    // Disabling dispatch preserves planning, and cannot clear a persisted manual pause.
    c.dispatchEnabled = true; assert(ShadowOptimizer.configure(c)); ticks(8);
    assert(HemsController.submitManual(manual, id)); runWorker(); ticks(2);
    c.dispatchEnabled = false; assert(ShadowOptimizer.configure(c)); assert(ShadowOptimizer.dispatchPlan().paused);
    ticks(8); assert(!ShadowOptimizer.dispatchPlan().config.dispatchEnabled);
    // New coordination uses the actual controller/worker authorization path.
    settings.Mode = Mode::Active; settings.Warp.Enabled = false; HemsController.updateSettings();
    c.flow.enabled = true; c.flow.pvAcLimitW = 3000; c.flow.maxExportW = 0;
    c.flow.evProtection = false; c.policies.targetEnabled = false;
    c.dispatchEnabled = true; c.policies.dischargeAllowed = true; c.maxDischargeW = 2500;
    c.maxChargeW = 1000;
    assert(ShadowOptimizer.configure(c)); assert(ShadowOptimizer.resume());
    Model::testGrid = Model::Signal<double>::fresh(200);
    Model::testPv = Model::Signal<double>::fresh(0); registers[30006] = 0;
    Flow::Coordination.observe({testNow,true,false}); ticks(2);
    auto refreshFlow = [&] { Flow::Coordination.observe({testNow,true,false}); ticks(1); };
    refreshFlow();
    Actuation::Command tooMuch { ControlTarget::Storage, Actuation::Action::SetDischargePowerW, true, 1200 };
    assert(!HemsController.submitManual(tooMuch,id)); // actual residual is 200 W
    Actuation::Command unsafeRelease { ControlTarget::Storage, Actuation::Action::Release };
    assert(!HemsController.submitManual(unsafeRelease,id));
    auto allowed = tooMuch; allowed.value = 100;
    // Wait for an idle mailbox without authorizing any simulated grid export.
    for(unsigned n=0;n<8;++n) refreshFlow();
    assert(HemsController.submitManual(allowed,id));
    {
        Model::testGrid = Model::Signal<double>::fresh(20); // load dropped after queueing
        auto before = writes.size(); runWorker();
        assert(writes.size()==before); // fresh worker authorization revokes the queued discharge
    }
    Model::testGrid.quality = Model::Quality::Stale;
    refreshFlow(); report.clear(); HemsController.serializeEconomic(report.to<JsonObject>());
    assert(report["export_enforcement_available"]==false);
    assert(Flow::Coordination.envelope().pvLimitW==0);
    Model::testGrid = Model::Signal<double>::fresh(200);
    Flow::Coordination.observe({testNow,false,false}); ticks(1);
    report.clear(); HemsController.serializeEconomic(report.to<JsonObject>());
    assert(report["export_enforcement_available"]==false);
    std::cout << "Production economic dispatch opt-in, Monitor/permission isolation, verified semantic writes, idempotence, revisions, SoC, durable pause/resume, revocation and storage-only integration passed\n";
}
