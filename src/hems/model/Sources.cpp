// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/model/Sources.h>
#include <hems/model/Normalize.h>
#include <Configuration.h>
#include <Hoymiles.h>
#include <powermeter/Controller.h>

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
            classificationComplete = false;
            continue;
        }
        if (classification->PowerSource != PowerLimiterInverterConfig::InverterPowerSource::Solar) {
            continue;
        }

        auto statistics = inverter->Statistics();
        if (statistics->getLastUpdate() == 0) { continue; }

        ++observedSolarSources;
        totalPowerW += statistics->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);
        if (!inverter->isReachable() || millis() - statistics->getLastUpdate() > 10000) { anyStale = true; }
    }

    return Hems::Model::normalizePvPower(totalPowerW, configuredSolarSources,
            observedSolarSources, classificationComplete, anyStale);
}

} // namespace

namespace Hems::Model {
void normalizeSources(SystemState& state)
{
    auto grid = PowerMeter.getSnapshot();
    state.grid.powerW = normalizeGridPower(grid.powerTotal, grid.valid && millis() - grid.lastUpdate <= 5000, grid.lastUpdate > 0);
    state.pv.powerW = normalizedPvPower();
}
} // namespace Hems::Model
