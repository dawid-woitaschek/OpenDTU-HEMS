// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/model/Normalize.h>

namespace {

Hems::Model::Quality quality(bool valid, bool stale, bool hasUpdate)
{
    if (stale && hasUpdate) { return Hems::Model::Quality::Stale; }
    if (valid) { return Hems::Model::Quality::Fresh; }
    return Hems::Model::Quality::Unknown;
}

Hems::Model::Signal<double> number(Hems::Storage::NumberValue const& value, Hems::Model::Quality state)
{
    if (!value.valid || state == Hems::Model::Quality::Unknown) { return {}; }
    return { value.value, state };
}

Hems::Model::Signal<bool> boolSignal(Hems::Storage::BoolValue const& value, Hems::Model::Quality state)
{
    if (!value.valid || state == Hems::Model::Quality::Unknown) { return {}; }
    return { value.value, state };
}

} // namespace

namespace Hems::Model {

Signal<double> normalizeGridPower(std::optional<float> powerW, bool valid, bool hasUpdate)
{
    if (!powerW || !hasUpdate) { return {}; }
    return { static_cast<double>(*powerW), valid ? Quality::Fresh : Quality::Stale };
}

Signal<double> normalizePvPower(double totalPowerW, uint8_t configuredSolarSources,
        uint8_t observedSolarSources, bool classificationComplete, bool anyStale)
{
    if (!classificationComplete || configuredSolarSources == 0
            || observedSolarSources != configuredSolarSources) {
        return {};
    }

    return { totalPowerW, anyStale ? Quality::Stale : Quality::Fresh };
}

SystemState normalize(Warp::Snapshot const& warp, Storage::Snapshot const& storage)
{
    SystemState result;

    result.evse.device.configured = warp.configured;
    result.evse.device.enabled = warp.enabled;
    result.evse.device.reachable = warp.reachable;
    result.evse.device.hasUpdate = warp.hasUpdate;
    result.evse.device.ageMs = warp.ageMs;

    auto warpQuality = quality(warp.valid, warp.stale, warp.hasUpdate);
    if (warpQuality != Quality::Unknown) {
        auto const& sample = warp.sample;
        result.evse.allowedCurrentA = { sample.allowedCurrentA, warpQuality };
        if (sample.vehicleKnown) { result.evse.vehicleConnected = { sample.vehicleConnected, warpQuality }; }
        if (sample.chargingKnown) { result.evse.charging = { sample.charging, warpQuality }; }
        if (sample.meterAvailable) { result.evse.powerW = { sample.powerW, warpQuality }; }
    }

    result.storage = normalizeStorage(storage);

    return result;
}

StorageState normalizeStorage(Storage::Snapshot const& storage)
{
    StorageState result;
    result.device.configured = storage.configured;
    result.device.enabled = storage.enabled;
    result.device.reachable = storage.reachable;
    result.device.hasUpdate = storage.hasUpdate;
    result.device.ageMs = storage.ageMs;

    auto storageQuality = quality(storage.valid, storage.stale, storage.hasUpdate);
    auto const& sample = storage.sample;
    result.socPct = number(sample.socPct, storageQuality);
    result.acPowerW = number(sample.acPowerW, storageQuality);
    result.dcPowerW = number(sample.dcPowerW, storageQuality);
    result.batteryVoltageV = number(sample.batteryVoltageV, storageQuality);
    result.batteryCurrentA = number(sample.batteryCurrentA, storageQuality);
    result.temperatureC = number(sample.temperatureC, storageQuality);
    result.charging = boolSignal(sample.charging, storageQuality);
    result.discharging = boolSignal(sample.discharging, storageQuality);
    result.faultActive = boolSignal(sample.faultActive, storageQuality);
    result.alarmActive = boolSignal(sample.alarmActive, storageQuality);

    return result;
}

} // namespace Hems::Model
