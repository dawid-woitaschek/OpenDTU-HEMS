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
namespace Hems::Optimizer {
namespace {
constexpr char Path[] = "/hems-optimizer.json", Temp[] = "/hems-optimizer.tmp";
Inputs liveInputs()
{
    Inputs in;
    in.now = time(nullptr);
    in.controlRevision = HemsController.economicContextRevision();
    in.tariffStatus = Energy::EnergyData.status(Energy::Domain::Tariff);
    in.forecastStatus = Energy::EnergyData.status(Energy::Domain::Forecast);
    in.storage = HemsController.getNormalizedState().storage;
    in.currentConstraints = HemsController.getEvProtectionShadowSnapshot().proposedConstraints.storage;
    auto constraints = HemsController.getConstraints().storage;
    in.currentConstraints.chargeAllowed = in.currentConstraints.chargeAllowed && constraints.chargeAllowed;
    in.currentConstraints.dischargeAllowed = in.currentConstraints.dischargeAllowed && constraints.dischargeAllowed;
    in.currentConstraints.maxChargePower = constraints.maxChargePower;
    in.currentConstraints.maxDischargePower = constraints.maxDischargePower;
    return in;
}
bool sameStorage(Inputs const& a, Inputs const& b)
{
    return a.controlRevision == b.controlRevision
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
    ++_generation; ++_planRevision; _result.reset(); _pending = true; _replanReason = "requested"; return true;
}
char const* Service::changedLocked(Inputs const& inputs, uint32_t revision) const
{
    if (!_result) { return _replanReason; }
    if (revision != _result->energyRevision) { return "energy_revision"; }
    if (inputs.controlRevision != _result->inputs.controlRevision) { return "control_changed"; }
    if (!sameStorage(inputs, _result->inputs)) { return "storage_or_constraints_changed"; }
    if (!sameStatus(inputs.tariffStatus, _result->inputs.tariffStatus)
            || (_result->plan.pvIncluded && !sameStatus(inputs.forecastStatus, _result->inputs.forecastStatus))) { return "input_freshness_changed"; }
    if (inputs.now < _result->plan.generatedAt || (_result->plan.count && inputs.now >= _result->plan.steps[0].end)
            || inputs.now / 900 != _result->inputs.now / 900) { return "current_interval_changed"; }
    return nullptr;
}
bool Service::due()
{
    auto revision = Energy::EnergyData.revision(); auto inputs = liveInputs();
    std::lock_guard<std::mutex> lock(_mutex);
    if (_config.enabled) {
        if (auto reason = changedLocked(inputs, revision)) { _pending = true; _replanReason = reason; }
    }
    return _pending && (!_attempted || millis() - _lastRunMs >= 5000);
}
void Service::run()
{
    Config c; uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pending || !_config.enabled || (_attempted && millis() - _lastRunMs < 5000)) { return; }
        _pending = false; c = _config; generation = _generation; _lastRunMs = millis(); _attempted = true;
    }
    Diagnostics::Scope activity(Diagnostics::Component::Optimizer, Diagnostics::Operation::Calculate);
    auto result = std::unique_ptr<Result>(new (std::nothrow) Result());
    if (!result) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (generation == _generation) { _error = "insufficient_memory"; _result.reset(); }
        return;
    }
    result->config = c; result->inputs = liveInputs(); result->energyRevision = Energy::EnergyData.revision();
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
    if (generation != _generation || latestRevision != result->energyRevision || !sameStorage(result->inputs, latestInputs)
            || latestInputs.now / 900 != result->inputs.now / 900) { _pending = _config.enabled; return; }
    _energyRevision = latestRevision; _result = std::move(result); _error = nullptr; ++_planRevision;
}
PlanView Service::dispatchPlan() const
{
    auto inputs = liveInputs(); auto revision = Energy::EnergyData.revision();
    std::lock_guard<std::mutex> lock(_mutex);
    PlanView view; view.config = _config; view.paused = _manualPaused;
    view.revision = _planRevision; view.energyRevision = revision;
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
