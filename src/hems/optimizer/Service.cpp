// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Service.h>
#include <hems/energy/Service.h>
#include <hems/Controller.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <ctime>
#include <new>
namespace Hems::Optimizer {
namespace {
constexpr char Path[] = "/hems-optimizer.json", Temp[] = "/hems-optimizer.tmp";
Inputs liveInputs()
{
    Inputs in;
    in.now = time(nullptr);
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
    return a.storage.socPct.quality == b.storage.socPct.quality && a.storage.socPct.value == b.storage.socPct.value
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
} // namespace
Service ShadowOptimizer;
void Service::init()
{
    auto file = LittleFS.open(Path, "r");
    if (file && file.size() <= 2048) {
        JsonDocument doc;
        if (!deserializeJson(doc, file) && doc["version"] == 1) { parseConfig(doc["config"], _config); }
    }
    Energy::EnergyData.background([](void* p) { return static_cast<Service*>(p)->due(); },
        [](void* p) { static_cast<Service*>(p)->run(); }, this);
}
bool Service::configure(Config const& c)
{
    JsonDocument doc; doc["version"] = 1; serializeConfig(c, doc["config"].to<JsonObject>());
    Config validated; if (!parseConfig(doc["config"], validated)) { return false; }
    std::lock_guard<std::mutex> lock(_mutex);
    LittleFS.remove(Temp); auto file = LittleFS.open(Temp, "w"); auto length = measureJson(doc);
    bool ok = file && !doc.overflowed() && length <= 2048 && serializeJson(doc, file) == length;
    if (file) { file.flush(); file.close(); }
    auto verify = LittleFS.open(Temp, "r"); JsonDocument check;
    ok = ok && verify && verify.size() == length && !deserializeJson(check, verify) && check["version"] == 1
        && parseConfig(check["config"], validated);
    verify.close();
    if (!ok || !LittleFS.rename(Temp, Path)) { LittleFS.remove(Temp); return false; }
    _config = c; ++_generation; _result.reset(); _pending = c.enabled; _error = nullptr;
    return true;
}
bool Service::requestRecalculate()
{
    std::lock_guard<std::mutex> lock(_mutex); if (!_config.enabled) { return false; } _pending = true; return true;
}
bool Service::due()
{
    auto revision = Energy::EnergyData.revision();
    std::lock_guard<std::mutex> lock(_mutex);
    if (_config.enabled && (!_attempted || millis() - _lastRunMs >= 60000 || revision != _energyRevision)) { _pending = true; }
    return _pending;
}
void Service::run()
{
    Config c; uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pending || !_config.enabled) { return; }
        _pending = false; c = _config; generation = _generation; _lastRunMs = millis(); _attempted = true;
    }
    auto result = std::unique_ptr<Result>(new (std::nothrow) Result());
    if (!result) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (generation == _generation) { _error = "insufficient_memory"; _result.reset(); }
        return;
    }
    result->config = c; result->inputs = liveInputs(); result->energyRevision = Energy::EnergyData.revision();
    Energy::EnergyData.snapshot(Energy::Domain::Tariff, result->tariff, result->inputs.tariffStatus);
    Energy::EnergyData.snapshot(Energy::Domain::Forecast, result->forecast, result->inputs.forecastStatus);
    result->inputs.tariff = &result->tariff; result->inputs.forecast = &result->forecast;
    uint32_t start = micros(); calculate(c, result->inputs, result->plan); result->plan.runtimeUs = micros() - start;
    auto latestRevision = Energy::EnergyData.revision(); auto latestInputs = liveInputs();
    std::lock_guard<std::mutex> lock(_mutex);
    if (generation != _generation || latestRevision != result->energyRevision || !sameStorage(result->inputs, latestInputs)
            || latestInputs.now / 900 != result->inputs.now / 900) { _pending = _config.enabled; return; }
    _energyRevision = latestRevision; _result = std::move(result); _error = nullptr;
}
void Service::serialize(JsonObject output, bool all, size_t offset) const
{
    auto inputs = liveInputs(); auto revision = Energy::EnergyData.revision();
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_result) {
        serializeConfig(_config, output["config"].to<JsonObject>()); output["shadow_only"] = true; output["device_writes"] = false;
        output["state"] = _config.enabled ? "not_ready" : "disabled";
        auto blockers = output["blockers"].to<JsonArray>();
        if (_config.enabled) { blockers.add(_error ? _error : "waiting_for_calculation"); }
        output["storage_soc_fresh"] = inputs.storage.socPct.usable() && inputs.storage.device.reachable;
        output["current_discharge_allowed"] = inputs.currentConstraints.dischargeAllowed;
        output["schedule"].to<JsonArray>(); return;
    }
    bool stale = revision != _result->energyRevision || !sameStorage(inputs, _result->inputs)
        || inputs.now < _result->plan.generatedAt || inputs.now - _result->plan.generatedAt > 120;
    serializePlan(_result->config, _result->inputs, _result->plan, output, stale ? 0 : all ? 24 : 12, offset);
    output["stale_inputs"] = stale;
    if (stale) { output["state"] = "stale_inputs"; output["blockers"].as<JsonArray>().add("inputs_changed_recalculation_required"); }
}
} // namespace Hems::Optimizer
