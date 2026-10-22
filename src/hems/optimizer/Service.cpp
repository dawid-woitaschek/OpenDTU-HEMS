// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Service.h>
#include <RuntimeDiagnostics.h>
#include <hems/energy/Service.h>
#include <hems/Controller.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <ctime>
#include <new>
#include <cmath>
#include <type_traits>
#include <hems/flow/Coordinator.h>
namespace Hems::Optimizer {
namespace {
constexpr char Path[] = "/hems-optimizer.json", Temp[] = "/hems-optimizer.tmp";
Inputs liveInputs()
{
    Inputs in;
    in.now = time(nullptr);
    auto dpl = Flow::Coordination.dpl();
    in.observedPvAcLimitW = dpl.physicalAcW;
    in.flowTopologySupported = dpl.atMs && millis() - dpl.atMs <= 3000 && dpl.topologySupported;
    in.controlRevision = HemsController.economicContextRevision();
    in.tariffStatus = Energy::EnergyData.status(Energy::Domain::Tariff);
    in.forecastStatus = Energy::EnergyData.status(Energy::Domain::Forecast);
    in.storage = HemsController.getStoragePlanningState();
    in.storageLimits = HemsController.getStoragePowerLimits();
    in.currentConstraints = HemsController.getEvProtectionShadowSnapshot().proposedConstraints.storage;
    auto constraints = HemsController.getConstraints().storage;
    in.currentConstraints.chargeAllowed = in.currentConstraints.chargeAllowed && constraints.chargeAllowed;
    in.currentConstraints.dischargeAllowed = in.currentConstraints.dischargeAllowed && constraints.dischargeAllowed;
    in.currentConstraints.maxChargePower = constraints.maxChargePower;
    in.currentConstraints.maxDischargePower = constraints.maxDischargePower;
    return in;
}
bool sameStorage(Inputs const& a, Inputs const& b, bool flow)
{
    return a.storageLimits.known == b.storageLimits.known && a.storageLimits.chargeW == b.storageLimits.chargeW
        && a.storageLimits.dischargeW == b.storageLimits.dischargeW && (!flow || (a.flowTopologySupported == b.flowTopologySupported && a.observedPvAcLimitW == b.observedPvAcLimitW))
        && a.loadRevision == b.loadRevision && a.controlRevision == b.controlRevision
        && a.storage.socPct.quality == b.storage.socPct.quality
        && std::isfinite(a.storage.socPct.value) == std::isfinite(b.storage.socPct.value)
        && std::abs(a.storage.socPct.value - b.storage.socPct.value) < 1.0
        && a.storage.device.reachable == b.storage.device.reachable && a.storage.device.enabled == b.storage.device.enabled
        && a.storage.device.hasUpdate == b.storage.device.hasUpdate
        && a.storage.faultActive.quality == b.storage.faultActive.quality && a.storage.faultActive.value == b.storage.faultActive.value
        && a.currentConstraints.chargeAllowed == b.currentConstraints.chargeAllowed
        && a.currentConstraints.dischargeAllowed == b.currentConstraints.dischargeAllowed
        && a.currentConstraints.maxChargePower.active == b.currentConstraints.maxChargePower.active
        && a.currentConstraints.maxChargePower.valueW == b.currentConstraints.maxChargePower.valueW
        && a.currentConstraints.maxDischargePower.active == b.currentConstraints.maxDischargePower.active
        && a.currentConstraints.maxDischargePower.valueW == b.currentConstraints.maxDischargePower.valueW;
}
bool sameStatus(Energy::Status const& a, Energy::Status const& b)
{
    return a.enabled == b.enabled && a.fresh == b.fresh && a.planningUsable == b.planningUsable
        && a.currentCovered == b.currentCovered;
}
} // namespace
Service ShadowOptimizer;
void Service::init()
{
    {
        auto history = LittleFS.open("/hems-load.bin", "r");
        uint32_t hash = 0;
        if (history && history.size() == sizeof(Load::History) + sizeof(hash)
                && history.readBytes(reinterpret_cast<char*>(&_load.history), sizeof(Load::History)) == sizeof(Load::History)
                && history.readBytes(reinterpret_cast<char*>(&hash), sizeof(hash)) == sizeof(hash)
                && _load.history.version == 1 && Load::checksum(_load.history) == hash) { }
        else { _load.history = {}; }
    }
    auto file = LittleFS.open(Path, "r");
    if (file && file.size() <= 2048) {
        JsonDocument doc;
        if (!deserializeJson(doc, file) && doc["version"] == 1 && parseConfig(doc["config"], _config)) {
            // Missing legacy pause defaults false. Malformed present pause fails closed.
            _manualPaused = !doc["manual_pause"].isUnbound()
                && (!doc["manual_pause"].is<bool>() || doc["manual_pause"].as<bool>());
        }
    }
    Energy::EnergyData.background([](void* p) { return static_cast<Service*>(p)->due(); },
        [](void* p) { static_cast<Service*>(p)->run(); }, this);
}
bool Service::configure(Config const& c)
{
    Diagnostics::Scope activity(Diagnostics::Component::Configuration, Diagnostics::Operation::Write);
    std::lock_guard<std::mutex> lock(_mutex);
    if (!persistLocked(c, _manualPaused)) { return false; }
    _config = c; ++_generation; ++_planRevision; _result.reset(); _pending = c.enabled; _error = nullptr;
    _replanReason = "configuration_changed";
    return true;
}
bool Service::persistLocked(Config const& c, bool paused)
{
    JsonDocument doc; doc["version"] = 1; doc["manual_pause"] = paused;
    serializeConfig(c, doc["config"].to<JsonObject>());
    Config validated; if (!parseConfig(doc["config"], validated)) { return false; }
    LittleFS.remove(Temp); auto file = LittleFS.open(Temp, "w"); auto length = measureJson(doc);
    bool ok = file && !doc.overflowed() && length <= 2048 && serializeJson(doc, file) == length;
    if (file) { file.flush(); file.close(); }
    auto verify = LittleFS.open(Temp, "r"); JsonDocument check;
    ok = ok && verify && verify.size() == length && !deserializeJson(check, verify) && check["version"] == 1
        && parseConfig(check["config"], validated) && check["manual_pause"].is<bool>()
        && check["manual_pause"].as<bool>() == paused;
    verify.close();
    if (!ok || !LittleFS.rename(Temp, Path)) { LittleFS.remove(Temp); return false; }
    return true;
}
bool Service::pauseForManual()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_config.dispatchEnabled || _manualPaused) { return true; }
    if (!persistLocked(_config, true)) { _error = "manual_pause_persist_failed"; return false; }
    _manualPaused = true; ++_generation; ++_planRevision;
    _error = nullptr;
    _pending = _config.enabled; _replanReason = "manual_pause";
    return true;
}
bool Service::resume()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_config.dispatchEnabled || !_config.enabled) { return false; }
    if (!_manualPaused) { return true; }
    if (!persistLocked(_config, false)) { _error = "resume_persist_failed"; return false; }
    _manualPaused = false; ++_generation; ++_planRevision; _result.reset();
    _error = nullptr;
    _pending = true; _replanReason = "manual_resume";
    return true;
}
void Service::invalidate(char const* reason)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ++_generation; ++_planRevision; _result.reset(); _pending = _config.enabled; _replanReason = reason;
}
bool Service::requestRecalculate()
{
    std::lock_guard<std::mutex> lock(_mutex); if (!_config.enabled) { return false; }
    // An explicit recalculation does not revoke an unchanged, still valid
    // interval. Live input changes revoke it independently in dispatchPlan().
    _pending = true; _replanReason = "requested"; return true;
}
char const* Service::changedLocked(Inputs const& inputs, uint32_t revision) const
{
    if (!_result) { return _replanReason; }
    if (revision != _result->energyRevision) { return "energy_revision"; }
    if (inputs.controlRevision != _result->inputs.controlRevision) { return "control_changed"; }
    if (!sameStorage(inputs, _result->inputs, _config.flow.enabled)) { return "storage_or_constraints_changed"; }
    if (!sameStatus(inputs.tariffStatus, _result->inputs.tariffStatus)
            || (_result->plan.pvIncluded && !sameStatus(inputs.forecastStatus, _result->inputs.forecastStatus))) { return "input_freshness_changed"; }
    if (inputs.now < _result->plan.generatedAt || (_result->plan.count && inputs.now >= _result->plan.steps[0].end)
            || inputs.now / 900 != _result->inputs.now / 900) { return "current_interval_changed"; }
    return nullptr;
}
bool Service::due()
{
    auto revision = Energy::EnergyData.revision(); auto inputs = liveInputs();
    bool observe = false;
    { std::lock_guard<std::mutex> lock(_mutex); observe = _config.automaticLoad && millis() - _lastObservationMs >= 1000; }
    auto state = observe ? HemsController.getNormalizedState() : Model::SystemState {};
    std::lock_guard<std::mutex> lock(_mutex);
    if (observe && _config.automaticLoad && millis() - _lastObservationMs >= 1000) {
        _lastObservationMs = millis(); _load.observe(state, inputs.now, millis());
    }
    inputs.loadRevision = _config.automaticLoad ? _load.revision : 0;
    _historyDue = _load.dirty && millis() - _lastHistorySaveMs >= 21600000;
    if (!_config.flow.evProtection) { inputs.currentConstraints.dischargeAllowed = true; }
    if (_config.enabled) {
        if (auto reason = changedLocked(inputs, revision)) { _pending = true; _replanReason = reason; }
    }
    return (_pending || _historyDue) && (!_attempted || millis() - _lastRunMs >= 5000);
}
void Service::run()
{
    Config c; uint32_t generation; bool saveHistory = false, planning = false;
    bool needProfile = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if ((!_pending && !_historyDue) || (_attempted && millis() - _lastRunMs < 5000)) { return; }
        needProfile = _config.automaticLoad || _historyDue;
    }
    auto load = std::unique_ptr<Load::Profile>(needProfile ? new (std::nothrow) Load::Profile() : nullptr);
    if (needProfile && !load) { return; }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if ((!_pending && !_historyDue) || (_attempted && millis() - _lastRunMs < 5000)) { return; }
        if ((_config.automaticLoad || _historyDue) && !load) { return; } // A concurrent config change retries later.
        if (load) { *load = _load; }
        saveHistory = _historyDue; planning = _pending && _config.enabled;
        _historyDue = false; _pending = false; c = _config; generation = _generation;
        _lastRunMs = millis(); _attempted = true;
    }
    if (saveHistory) {
        // Bounded six-hour checkpoint on the existing worker, outside the
        // service lock. Scheduler readers never wait for flash writes.
        uint32_t hash = Load::checksum(load->history);
        auto file = LittleFS.open("/hems-load.tmp", "w");
        bool ok = file && file.write(reinterpret_cast<uint8_t const*>(&load->history), sizeof(Load::History)) == sizeof(Load::History)
            && file.write(reinterpret_cast<uint8_t const*>(&hash), sizeof(hash)) == sizeof(hash);
        if (file) { file.flush(); file.close(); }
        auto verify = LittleFS.open("/hems-load.tmp", "r");
        uint32_t checked = 2166136261u;
        for (size_t n = 0; ok && n < sizeof(Load::History); ++n) {
            int value = verify.read(); ok = value >= 0; checked = (checked ^ static_cast<uint8_t>(value)) * 16777619u;
        }
        uint32_t stored = 0;
        ok = ok && verify.readBytes(reinterpret_cast<char*>(&stored), sizeof(stored)) == sizeof(stored) && stored == checked;
        verify.close();
        ok = ok && LittleFS.rename("/hems-load.tmp", "/hems-load.bin");
        if (!ok) { LittleFS.remove("/hems-load.tmp"); }
        std::lock_guard<std::mutex> lock(_mutex);
        if (ok && _load.revision == load->revision) { _load.dirty = false; }
        if (!ok) { _error = "load_history_save_failed"; }
        _lastHistorySaveMs = millis();
    }
    if (!planning) { return; }
    Diagnostics::Scope activity(Diagnostics::Component::Optimizer, Diagnostics::Operation::Calculate);
    auto result = std::unique_ptr<Result>(new (std::nothrow) Result());
    if (!result) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (generation == _generation) { _error = "insufficient_memory"; _result.reset(); }
        return;
    }
    result->config = c; result->inputs = liveInputs();
    result->inputs.loadRevision = c.automaticLoad ? load->revision : 0;
    result->inputs.loadProfile = c.automaticLoad ? load.get() : nullptr;
    if (!c.flow.evProtection) { result->inputs.currentConstraints.dischargeAllowed = true; }
    result->energyRevision = Energy::EnergyData.revision();
    {
        auto tariff = std::unique_ptr<Energy::Series>(new (std::nothrow) Energy::Series());
        auto forecast = std::unique_ptr<Energy::Series>(new (std::nothrow) Energy::Series());
        if (!tariff || !forecast) {
            std::lock_guard<std::mutex> lock(_mutex);
            if (generation == _generation) { _error = "insufficient_memory"; _result.reset(); }
            return;
        }
        Energy::EnergyData.snapshot(Energy::Domain::Tariff, *tariff, result->inputs.tariffStatus);
        Energy::EnergyData.snapshot(Energy::Domain::Forecast, *forecast, result->inputs.forecastStatus);
        result->inputs.tariff = tariff.get(); result->inputs.forecast = forecast.get();
        uint32_t start = micros(); calculate(c, result->inputs, result->plan); result->plan.runtimeUs = micros() - start;
        result->inputs.tariff = result->inputs.forecast = nullptr;
    }
    auto latestRevision = Energy::EnergyData.revision(); auto latestInputs = liveInputs();
    std::lock_guard<std::mutex> lock(_mutex);
    latestInputs.loadRevision = c.automaticLoad ? _load.revision : 0;
    if (!c.flow.evProtection) { latestInputs.currentConstraints.dischargeAllowed = true; }
    if (generation != _generation || latestRevision != result->energyRevision || !sameStorage(result->inputs, latestInputs, c.flow.enabled)
            || latestInputs.now / 900 != result->inputs.now / 900) { _pending = _config.enabled; return; }
    if (_result && c.hysteresisMicros && result->plan.count) {
        evaluateRetained(c, result->inputs, result->plan, _result->plan, result->plan, static_cast<int64_t>(c.hysteresisMicros) * 1000);
    }
    result->inputs.loadProfile = nullptr;
    // Readers hold _mutex and no Result address escapes. Keep the published
    // allocation in place rather than moving this large, long-lived heap island
    // between short-lived allocations on every replan. Candidate validation and
    // failure behavior stay atomic; this adds only a bounded plain-data copy.
    static_assert(std::is_trivially_copy_assignable<Result>::value, "Publication must not allocate under the lock");
    _energyRevision = latestRevision;
    if (_result) { *_result = *result; }
    else { _result = std::move(result); }
    _error = nullptr; ++_planRevision;
}
PlanView Service::dispatchPlan() const
{
    auto inputs = liveInputs(); auto revision = Energy::EnergyData.revision();
    std::lock_guard<std::mutex> lock(_mutex);
    inputs.loadRevision = _config.automaticLoad ? _load.revision : 0;
    if (!_config.flow.evProtection) { inputs.currentConstraints.dischargeAllowed = true; }
    PlanView view; view.config = _config; view.paused = _manualPaused;
    view.revision = _planRevision; view.energyRevision = revision;
    view.inputsUsable = inputs.tariffStatus.fresh && inputs.tariffStatus.planningUsable && inputs.tariffStatus.currentCovered
        && (!_result || !_result->plan.pvIncluded || (inputs.forecastStatus.fresh && inputs.forecastStatus.planningUsable));
    if (_result && _result->plan.count) {
        auto const& current = _result->plan.steps[0];
        view.start = current.start; view.end = current.end; view.batteryW = current.batteryW;
    }
    if (!_config.enabled) { view.reason = "planner_disabled"; return view; }
    if (auto reason = changedLocked(inputs, revision)) { view.reason = reason; return view; }
    if (!_result || !_result->plan.count) { return view; }
    auto const& step = _result->plan.steps[0];
    view.start = step.start; view.end = step.end; view.batteryW = step.batteryW;
    view.valid = !_result->plan.blocker && inputs.now >= step.start && inputs.now < step.end
        && inputs.tariffStatus.fresh && inputs.tariffStatus.planningUsable
        && (!_result->plan.pvIncluded || (inputs.forecastStatus.fresh && inputs.forecastStatus.planningUsable));
    view.reason = view.valid ? "current_interval" : "plan_unavailable";
    return view;
}
void Service::serialize(JsonObject output, bool all, size_t offset) const
{
    auto inputs = liveInputs(); auto revision = Energy::EnergyData.revision();
    std::lock_guard<std::mutex> lock(_mutex);
    inputs.loadRevision = _config.automaticLoad ? _load.revision : 0;
    if (!_config.flow.evProtection) { inputs.currentConstraints.dischargeAllowed = true; }
    unsigned days = 0;
    for (auto const& day : _load.history.days) { days += day.civil != 0; }
    output["load_history_days"] = days; output["load_history_bytes"] = sizeof(Load::History);
    output["load_history_revision"] = _load.revision; output["load_history_dirty"] = _load.dirty;
    output["load_history_saved_ms"] = _lastHistorySaveMs;
    output["manual_paused"] = _manualPaused; output["plan_revision"] = _planRevision;
    output["configuration_generation"] = _generation; output["energy_revision"] = revision;
    output["replan_reason"] = _replanReason; output["replan_pending"] = _pending;
    output["last_error"] = _error;
    output["last_run_ms"] = _lastRunMs;
    output["plan_energy_revision"] = _result ? _result->energyRevision : 0;
    if (!_result) {
        serializeConfig(_config, output["config"].to<JsonObject>()); output["shadow_only"] = true; output["device_writes"] = false;
        output["state"] = _config.enabled ? "not_ready" : "disabled";
        auto blockers = output["blockers"].to<JsonArray>();
        if (_config.enabled) { blockers.add(_error ? _error : "waiting_for_calculation"); }
        output["storage_soc_fresh"] = inputs.storage.socPct.usable() && inputs.storage.device.reachable;
        output["current_discharge_allowed"] = inputs.currentConstraints.dischargeAllowed;
        output["schedule"].to<JsonArray>(); return;
    }
    bool stale = changedLocked(inputs, revision) != nullptr;
    serializePlan(_result->config, _result->inputs, _result->plan, output, stale ? 0 : all ? 24 : 12, offset);
    output["stale_inputs"] = stale;
    if (stale) { output["state"] = "stale_inputs"; output["blockers"].as<JsonArray>().add("inputs_changed_recalculation_required"); }
}
} // namespace Hems::Optimizer
