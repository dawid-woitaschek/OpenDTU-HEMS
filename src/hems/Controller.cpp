// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <hems/model/Normalize.h>
#include <hems/storage/SettingsStore.h>
#include <powermeter/Controller.h>
#include <Configuration.h>
#include <Hoymiles.h>

Hems::Controller HemsController;

namespace {

PowerLimiterInverterConfig const* findPowerSourceConfig(PowerLimiterConfig const& config, uint64_t serial)
{
    for (size_t i = 0; i < INV_MAX_COUNT; ++i) {
        auto const& candidate = config.Inverters[i];
        if (candidate.Serial == 0ULL) { break; }
        if (candidate.Serial == serial) { return &candidate; }
    }
    return nullptr;
}

Hems::Model::Signal<double> normalizedPvPower()
{
    auto const powerLimiter = Configuration.get().PowerLimiter;
    bool classificationComplete = true;
    bool anyStale = false;
    uint8_t configuredSolarSources = 0;
    uint8_t observedSolarSources = 0;
    double totalPowerW = 0.0;

    for (size_t i = 0; i < INV_MAX_COUNT; ++i) {
        auto const& candidate = powerLimiter.Inverters[i];
        if (candidate.Serial == 0ULL) { break; }

        for (size_t j = 0; j < i; ++j) {
            if (powerLimiter.Inverters[j].Serial == candidate.Serial) {
                classificationComplete = false;
                break;
            }
        }

        if (candidate.PowerSource == PowerLimiterInverterConfig::InverterPowerSource::Solar) {
            ++configuredSolarSources;
        }
    }

    for (uint8_t i = 0; i < Hoymiles.getNumInverters(); ++i) {
        auto inverter = Hoymiles.getInverterByPos(i);
        if (!inverter || !inverter->getEnablePolling()) { continue; }

        auto const* classification = findPowerSourceConfig(powerLimiter, inverter->serial());
        if (!classification) {
            // A poll-enabled inverter without a power-source classification could
            // itself be PV. Returning a partial sum would silently under-report PV.
            classificationComplete = false;
            continue;
        }
        if (classification->PowerSource != PowerLimiterInverterConfig::InverterPowerSource::Solar) {
            continue;
        }

        auto statistics = inverter->Statistics();
        if (statistics->getLastUpdate() == 0) {
            // Keep the source unobserved. The normalizer will return Unknown until
            // every configured solar source has produced at least one sample.
            continue;
        }

        ++observedSolarSources;
        totalPowerW += statistics->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);
        if (!inverter->isReachable()) { anyStale = true; }
    }

    return Hems::Model::normalizePvPower(totalPowerW, configuredSolarSources,
            observedSolarSources, classificationComplete, anyStale);
}

} // namespace

namespace Hems {

void Controller::init(Scheduler& scheduler)
{
    Storage::StorageSettings.init();
    scheduler.addTask(_loopTask);
    _loopTask.setCallback([this] {
        _warp.loop();
        _storage.loop();
    });
    _loopTask.setInterval(100);
    _loopTask.setIterations(TASK_FOREVER);
    _loopTask.enable();
    updateSettings();
}

void Controller::updateSettings()
{
    auto const& config = Configuration.get().Hems;
    // Unknown schemas must not enable HEMS or external polling.
    _mode.store(config.Version == CONFIG_VERSION_HEMS && config.Enabled
            ? Mode::Monitor : Mode::Disabled);
    bool monitor = getMode() == Mode::Monitor;
    _warp.configure(config.Warp, monitor);
    _storage.configure(Storage::StorageSettings.get(), monitor);
}

Mode Controller::getMode() const
{
    return _mode.load();
}

Warp::Snapshot Controller::getWarpSnapshot() const
{
    return _warp.snapshot();
}

Storage::Snapshot Controller::getStorageSnapshot() const
{
    return _storage.snapshot();
}

Model::SystemState Controller::getNormalizedState() const
{
    auto result = Model::normalize(_warp.snapshot(), _storage.snapshot());
    auto grid = PowerMeter.getSnapshot();
    result.grid.powerW = Model::normalizeGridPower(grid.powerTotal, grid.valid, grid.lastUpdate > 0);
    result.pv.powerW = normalizedPvPower();
    return result;
}

Constraints::ConstraintSet Controller::getConstraints() const
{
    // PR #6 establishes deterministic constraint composition. Policies begin in
    // PR #7, so the current aggregate intentionally contains no restrictions.
    return {};
}

} // namespace Hems
