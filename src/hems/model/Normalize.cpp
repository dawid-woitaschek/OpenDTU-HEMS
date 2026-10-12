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

Hems::Model::Signal<bool> boolean(Hems::Storage::BoolValue const& value, Hems::Model::Quality state)
{
    if (!value.valid || state == Hems::Model::Quality::Unknown) { return {}; }
    return { value.value, state };
}

} // namespace

namespace Hems::Model {

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

    result.storage.device.configured = storage.configured;
    result.storage.device.enabled = storage.enabled;
    result.storage.device.reachable = storage.reachable;
    result.storage.device.hasUpdate = storage.hasUpdate;
    result.storage.device.ageMs = storage.ageMs;

    auto storageQuality = quality(storage.valid, storage.stale, storage.hasUpdate);
    auto const& sample = storage.sample;
    result.storage.socPct = number(sample.socPct, storageQuality);
    result.storage.acPowerW = number(sample.acPowerW, storageQuality);
    result.storage.dcPowerW = number(sample.dcPowerW, storageQuality);
    result.storage.batteryVoltageV = number(sample.batteryVoltageV, storageQuality);
    result.storage.batteryCurrentA = number(sample.batteryCurrentA, storageQuality);
    result.storage.temperatureC = number(sample.temperatureC, storageQuality);
    result.storage.charging = boolean(sample.charging, storageQuality);
    result.storage.discharging = boolean(sample.discharging, storageQuality);
    result.storage.faultActive = boolean(sample.faultActive, storageQuality);
    result.storage.alarmActive = boolean(sample.alarmActive, storageQuality);

    // Grid and PV are intentionally left Unknown until their existing OpenDTU
    // sources are adapted into this model. Unknown is a first-class state and
    // must never be replaced by a fabricated zero.
    return result;
}

} // namespace Hems::Model
