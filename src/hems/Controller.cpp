// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <hems/evse/SettingsStore.h>
#include <hems/model/Normalize.h>
#include <hems/storage/SettingsStore.h>
#include <powermeter/Controller.h>
#include <Arduino.h>
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

bool Controller::providerAuthorization(void* context, ControlTarget target)
{
    return context && static_cast<Controller*>(context)->actuationAllowed(target);
}

void Controller::init(Scheduler& scheduler)
{
    Evse::BindingSettings.init();
    Storage::StorageSettings.init();
    _warp.setAuthorization(this, providerAuthorization);
    _storage.setAuthorization(this, providerAuthorization);

    scheduler.addTask(_loopTask);
    _loopTask.setCallback([this] {
        _warp.loop();
        _storage.loop();

        auto normalized = Model::normalize(_warp.snapshot(), _storage.snapshot());
        std::lock_guard<std::mutex> lock(_policyMutex);
        _evProtectionPolicy.update(normalized.evse, millis());
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
    _warp.configure(config.Warp, Evse::BindingSettings.get(), monitor);
    _storage.configure(Storage::StorageSettings.get(), monitor);
    if (!monitor) {
        std::lock_guard<std::mutex> lock(_policyMutex);
        _evProtectionPolicy.reset();
    }
}

Mode Controller::getMode() const
{
    return _mode.load();
}

ControlMode Controller::getControlMode(ControlTarget target) const
{
    switch (target) {
        case ControlTarget::Evse: return Evse::BindingSettings.get().Control;
        case ControlTarget::Storage: return Storage::StorageSettings.get().Control;
        default: return ControlMode::MonitorOnly;
    }
}

bool Controller::controlPermissionGranted(ControlTarget target) const
{
    return getControlMode(target) == ControlMode::HemsControl;
}

Actuation::Readiness Controller::getActuationReadiness(ControlTarget target) const
{
    Actuation::ReadinessInput input;
    // There is deliberately no global Active mode yet. Keeping this false makes
    // the worker bridge fully wired but impossible to authorize in production.
    input.globalActive = false;
    input.permissionGranted = controlPermissionGranted(target);

    switch (target) {
        case ControlTarget::Evse: {
            auto snapshot = _warp.snapshot();
            input.configured = snapshot.configured;
            input.enabled = snapshot.enabled;
            input.profileControllable = _warp.controlCapable();
            input.reachable = snapshot.reachable;
            input.telemetryFresh = snapshot.valid && !snapshot.stale;
            input.faulted = snapshot.valid && snapshot.sample.chargingState == 4;
            break;
        }
        case ControlTarget::Storage: {
            auto snapshot = _storage.snapshot();
            input.configured = snapshot.configured;
            input.enabled = snapshot.enabled;
            input.profileControllable = _storage.controlCapable();
            input.reachable = snapshot.reachable;
            input.telemetryFresh = snapshot.valid && !snapshot.stale;
            input.faulted = snapshot.valid
                && ((snapshot.sample.faultActive.valid && snapshot.sample.faultActive.value)
                    || (snapshot.sample.faultCode.valid && snapshot.sample.faultCode.value != 0));
            break;
        }
        default:
            break;
    }

    return Actuation::evaluateReadiness(input);
}

bool Controller::actuationAllowed(ControlTarget target) const
{
    return getActuationReadiness(target).allowed();
}

bool Controller::submitActuation(Actuation::Command const& command, uint32_t& id)
{
    id = 0;
    if (!Actuation::validCommand(command) || !actuationAllowed(command.target)) { return false; }
    switch (command.target) {
        case ControlTarget::Evse: return _warp.submitCommand(command, id);
        case ControlTarget::Storage: return _storage.submitCommand(command, id);
        default: return false;
    }
}

bool Controller::cancelPendingActuation(ControlTarget target, uint32_t id)
{
    switch (target) {
        case ControlTarget::Evse: return _warp.cancelPendingCommand(id);
        case ControlTarget::Storage: return _storage.cancelPendingCommand(id);
        default: return false;
    }
}

Actuation::MailboxSnapshot Controller::getActuationCommandSnapshot(ControlTarget target) const
{
    switch (target) {
        case ControlTarget::Evse: return _warp.commandSnapshot();
        case ControlTarget::Storage: return _storage.commandSnapshot();
        default: return {};
    }
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
    // The EV protection policy is deliberately Shadow-only in this milestone.
    // Proposed restrictions are observable separately and do not become effective
    // constraints until the later Active milestone.
    return {};
}

Policies::EvProtectionSnapshot Controller::getEvProtectionShadowSnapshot() const
{
    std::lock_guard<std::mutex> lock(_policyMutex);
    return _evProtectionPolicy.snapshot(millis());
}

} // namespace Hems
