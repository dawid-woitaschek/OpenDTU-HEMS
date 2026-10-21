// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Runtime.h"
#include <hems/optimizer/Service.h>
#include <hems/energy/Service.h>
#include <hems/actuation/Cleanup.h>
#include <ctime>
static uint32_t scenarioUtc = 1792180800;
extern "C" time_t time(time_t* out) noexcept { if (out) { *out = scenarioUtc; } return scenarioUtc; }
namespace Hems::Energy {
bool providerGet(char const*, Source, JsonDocument&, NativeInfo&, char const*& error) { error = "scenario_offline"; return false; }
}
namespace Scenario {
// Logs actual public mailbox/results and cleanup snapshots, never synthesized policy decisions.
class Harness {
public:
    Scheduler scheduler;
    JsonDocument log;
    StorageConfig storage;
    Evse::BindingConfig evse;
    Optimizer::Config economic;
    explicit Harness(bool protection = false)
    {
        auto battery = load("../device-profiles/storage/Marstek-Venus-E-3.0.json");
        auto charger = load("../device-profiles/evse/Tinkerforge-WARP3-Smart.json");
        selectedProfiles[battery.id] = battery; selectedProfiles[charger.id] = charger;
        auto& settings = Configuration.data.Hems; settings.Mode = Mode::Monitor;
        settings.Warp.Enabled = true; strcpy(settings.Warp.Host, "warp.invalid");
        strcpy(evse.Profile, charger.id); evse.Control = ControlMode::MonitorOnly; Evse::BindingSettings.set(evse);
        storage.Enabled = true; storage.Control = ControlMode::HemsControl;
        strcpy(storage.Host, "storage.invalid"); strcpy(storage.Profile, battery.id); Storage::StorageSettings.set(storage);
        log.to<JsonArray>();
        testNow = 1000; freshRegisters(); evCharging(false); storagePower(0);
        HemsController.init(scheduler); Energy::EnergyData.init(scheduler); Optimizer::ShadowOptimizer.init();
        observeBoundary = [this] { capture(); };
        advance(8000);
        if (protection) { mode(Mode::Active); }
    }
    ~Harness() { observeBoundary = {}; }
    void mode(Mode value) { Configuration.data.Hems.Mode = value; HemsController.updateSettings(); }
    void storagePower(int16_t watts) { registers[30006] = static_cast<uint16_t>(-watts); }
    void soc(double percent) { registers[34002] = percent * 10; }
    void evCharging(bool value) { inputRegisters[1003] = value ? 3 : 2; }
    void evUnknown() { inputRegisters[1003] = 99; }
    void connect(bool value) { NetworkSettings.connected = value; }
    // Both clocks advance deterministically; each tick runs actual scheduler and notified workers.
    void advance(uint32_t ms, bool workers = true)
    {
        uint32_t remaining = ms;
        while (remaining) {
            auto delta = std::min(remaining, uint32_t(1000)); testNow += delta;
            _utcRemainder += delta; scenarioUtc += _utcRemainder / 1000; _utcRemainder %= 1000;
            scheduler.execute(); capture(); if (workers) { runWorker(); } capture(); remaining -= delta;
        }
    }
    bool manual(Actuation::Action action, double value = 0)
    {
        Actuation::Command command {ControlTarget::Storage, action,
            action == Actuation::Action::SetChargePowerW || action == Actuation::Action::SetDischargePowerW || action == Actuation::Action::SetTargetSocPct, value};
        uint32_t id; bool accepted = HemsController.submitManual(command, id); capture(); runWorker(); capture(); return accepted;
    }
    void tariff(int32_t currentPrice = 0, int32_t futurePrice = 400000)
    {
        Energy::Settings settings; settings.enabled = true; settings.maxAgeSeconds = 86400;
        assert(Energy::EnergyData.configure(Energy::Domain::Tariff, settings));
        JsonDocument data; data["schema"] = Energy::schema(Energy::Domain::Tariff); data["source"] = "deterministic scenario";
        data["currency"] = "EUR"; data["generated_at"] = scenarioUtc;
        uint32_t start = scenarioUtc / 900 * 900;
        for (unsigned i = 0; i < 32; ++i) {
            auto row = data["intervals"].add<JsonObject>(); row["start"] = start + i * 900; row["end"] = start + (i + 1) * 900;
            row["price_micros_per_kwh"] = i == 0 ? currentPrice : futurePrice;
        }
        assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, data.as<JsonVariantConst>()));
    }
    void planning(bool dispatch = false)
    {
        tariff(); economic.enabled = economic.socBoundsConfigured = true; economic.dispatchEnabled = dispatch;
        economic.effectivePrice = false; economic.capacityWh = 2000; economic.minSoc = 20; economic.maxSoc = 90;
        economic.maxChargeW = economic.maxDischargeW = 2000; economic.chargePermille = economic.dischargePermille = 950;
        economic.baseLoadW = 1000; economic.horizonSeconds = 14400;
        assert(Optimizer::ShadowOptimizer.configure(economic)); advance(8000);
    }
    size_t commands(Actuation::Action action, Actuation::Origin origin) const
    {
        size_t count = 0;
        for (auto row : log.as<JsonArrayConst>()) {
            if (row["action"] == Actuation::actionName(action) && row["source"] == Actuation::originName(origin)
                && row["state"] == "succeeded") { ++count; }
        }
        return count;
    }
    void capture()
    {
        for (auto target : {ControlTarget::Storage, ControlTarget::Evse}) {
            auto s = HemsController.getActuationCommandSnapshot(target);
            auto index = target == ControlTarget::Storage ? 0 : 1;
            std::string key = std::to_string(s.generation) + ":" + std::to_string(s.id) + ":" + std::to_string(int(s.state));
            if (s.id && key != _last[index]) {
                _last[index] = key;
                auto row = log.as<JsonArray>().add<JsonObject>();
                row["at_ms"] = testNow; row["utc"] = scenarioUtc;
                row["target"] = index == 0 ? "storage" : "evse"; row["id"] = s.id;
                row["action"] = Actuation::actionName(s.command.action); row["value"] = nullptr; if (s.command.hasValue) { row["value"] = s.command.value; }
                row["source"] = Actuation::originName(s.command.origin); row["authority"] = s.command.authority;
                row["generation"] = s.generation; row["state"] = Actuation::mailboxStateName(s.state);
                if (s.state == Actuation::MailboxState::Succeeded || s.state == Actuation::MailboxState::Failed) {
                    auto result = HemsController.getActuationCommandResult(target);
                    row["result"] = Actuation::executionErrorName(result.execution.error);
                    row["command_error"] = Actuation::commandErrorName(result.error);
                    row["stage"] = Actuation::commandStageName(result.stage);
                    row["transport_error"] = static_cast<unsigned>(result.execution.transport.error);
                    row["readback_verified"] = result.execution.readbackVerified;
                    row["write_attempted"] = result.execution.writeAttempted;
                }
            }
            auto cleanup = HemsController.getActuationCleanupSnapshot(target);
            auto cleanupKey = std::to_string(cleanup.attempts) + ":" + std::to_string(int(cleanup.state));
            if (cleanup.attempts && cleanupKey != _lastCleanup[index]) {
                _lastCleanup[index] = cleanupKey;
                auto row = log.as<JsonArray>().add<JsonObject>(); row["at_ms"] = testNow;
                row["target"] = index == 0 ? "storage" : "evse"; row["action"] = "cleanup_release";
                row["attempts"] = cleanup.attempts; row["state"] = Actuation::cleanupStateName(cleanup.state);
                row["result"] = Actuation::executionErrorName(cleanup.lastResult.execution.error);
            }
        }
    }
private:
    uint32_t _utcRemainder = 0;
    std::string _last[2], _lastCleanup[2];
};
} // namespace Scenario
