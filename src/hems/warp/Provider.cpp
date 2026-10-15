// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/warp/Provider.h>
#include <hems/evse/Poller.h>
#include <hems/profiles/FlashStore.h>
#include <hems/warp/Poller.h>
#include <hems/warp/ProfileAdapter.h>
#include <NetworkSettings.h>
#include <esp_log.h>
#include <new>

#undef TAG
static const char* TAG = "hems-warp";

namespace {

Hems::Actuation::CommandResult cleanupTransportFailure(Hems::Modbus::Result transport)
{
    Hems::Actuation::CommandResult result;
    result.error = Hems::Actuation::CommandError::ExecutionFailed;
    result.stage = Hems::Actuation::CommandStage::Release;
    result.execution.error = Hems::Actuation::ExecutionError::WriteFailed;
    result.execution.transport = transport;
    return result;
}

} // namespace

namespace Hems::Warp {

void Provider::retainCleanupLocked(WarpConfig const& config,
        std::shared_ptr<Profiles::Profile const> const& profile, uint8_t unit)
{
    if (_cleanup.valid || !profile || !config.Host[0] || !config.Port || !unit) { return; }

    _cleanup.valid = true;
    _cleanup.config = config;
    _cleanup.profile = profile;
    _cleanup.unit = unit;
    _cleanup.token = _nextCleanupToken++;
    if (_cleanup.token == 0) { _cleanup.token = _nextCleanupToken++; }
    if (_nextCleanupToken == 0) { _nextCleanupToken = 1; }
    _cleanupState = Actuation::CleanupState::Pending;
    _cleanupAttempts = 0;
    _cleanupLastAttemptMs = 0;
    _cleanupLastResult = {};
}

void Provider::configure(WarpConfig const& config, Evse::BindingConfig const& binding, bool monitor)
{
    bool requested = binding.Profile[0] != '\0';
    std::shared_ptr<Profiles::Profile const> profile;
    if (requested) {
        auto candidate = std::shared_ptr<Profiles::Profile>(new (std::nothrow) Profiles::Profile());
        if (candidate) {
            bool builtIn = false;
            char const* error = nullptr;
            std::lock_guard<std::mutex> storeLock(Profiles::runtimeStoreMutex());
            bool found = Profiles::runtimeStore().get(binding.Profile, *candidate, builtIn, error);
            if (found && !builtIn && Evse::supportsProfile(*candidate)) {
                profile = std::move(candidate);
            }
        }
    }

    uint8_t unit = profile ? profile->unit : 1;
    bool profileReady = !requested || static_cast<bool>(profile);

    _mailbox.cancelPending();

    std::lock_guard<std::mutex> lock(_mutex);
    if (_controlOwned && !_cleanup.valid) {
        retainCleanupLocked(_config, _profile, _unit);
    }
    _controlOwned = false;

    _config = config;
    _profile = std::move(profile);
    _profileRequested = requested;
    _unit = unit;
    _enabled = monitor && config.Enabled && config.Host[0] != '\0' && config.Port != 0 && profileReady;
    ++_generation;
    _polled = false;
    _state.reset(config.Enabled, _enabled);
}

Snapshot Provider::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto result = _state.snapshot(millis());
    result.profileConfigured = _profileRequested;
    result.profileLoaded = static_cast<bool>(_profile);
    result.profileDriven = result.profileLoaded;
    return result;
}

bool Provider::controlCapable() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _profile && Profiles::hasControl(*_profile);
}

Actuation::CleanupSnapshot Provider::cleanupSnapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return { _cleanupState, _cleanup.valid, _cleanupAttempts, _cleanupLastResult };
}

void Provider::setAuthorization(void* context, Actuation::AuthorizationCallback callback)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _authorizationContext = context;
    _authorization = callback;
}

bool Provider::commandAuthorized(uint32_t generation) const
{
    void* context = nullptr;
    Actuation::AuthorizationCallback callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_enabled || !_profile || _cleanup.valid || generation != _generation) { return false; }
        context = _authorizationContext;
        callback = _authorization;
    }
    return callback && callback(context, ControlTarget::Evse);
}

bool Provider::cleanupAuthorized(uint32_t token) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _cleanup.valid && token != 0 && token == _cleanup.token
        && _cleanupState == Actuation::CleanupState::Running;
}

bool Provider::ensureWorkerLocked()
{
    if (_worker) { return true; }
    return xTaskCreate(workerEntry, "HEMS:WARP", 4096, this, 1, &_worker) == pdPASS;
}

bool Provider::submitCommand(Actuation::Command const& command, uint32_t& id)
{
    id = 0;
    if (command.target != ControlTarget::Evse || !Actuation::validCommand(command)) { return false; }

    uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_enabled || !_profile || _cleanup.valid) { return false; }
        generation = _generation;
    }
    if (!commandAuthorized(generation)) { return false; }
    if (!_mailbox.submit(command, id, generation)) { return false; }

    TaskHandle_t worker = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (generation == _generation && !_cleanup.valid && ensureWorkerLocked()) { worker = _worker; }
    }
    if (!worker) {
        _mailbox.cancelPending(id);
        id = 0;
        return false;
    }
    xTaskNotifyGive(worker);
    return true;
}

bool Provider::cancelPendingCommand(uint32_t id)
{
    return _mailbox.cancelPending(id);
}

Actuation::MailboxSnapshot Provider::commandSnapshot() const
{
    return _mailbox.snapshot();
}

void Provider::loop()
{
    auto commandState = _mailbox.snapshot().state;
    if (commandState == Actuation::MailboxState::Pending
            || commandState == Actuation::MailboxState::Running) {
        return;
    }

    TaskHandle_t worker = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_busy) { return; }

        uint32_t now = millis();
        bool cleanupDue = _cleanup.valid
            && (_cleanupState == Actuation::CleanupState::Pending
                || (_cleanupState == Actuation::CleanupState::RetryWait
                    && now - _cleanupLastAttemptMs >= Actuation::CleanupRetryMs));
        if (cleanupDue) {
            if (!ensureWorkerLocked()) {
                ++_cleanupAttempts;
                _cleanupLastAttemptMs = now;
                _cleanupLastResult = cleanupTransportFailure({ Modbus::Error::TaskUnavailable });
                _cleanupState = Actuation::CleanupState::RetryWait;
                return;
            }
            _cleanupState = Actuation::CleanupState::Running;
            _busy = true;
            worker = _worker;
        } else if (_enabled && (!_polled || now - _lastPollMs >= PollIntervalMs)) {
            if (!ensureWorkerLocked()) {
                _state.apply({}, { Modbus::Error::TaskUnavailable }, now);
                _polled = true;
                _lastPollMs = now;
                return;
            }
            _busy = true;
            _polled = true;
            _lastPollMs = now;
            worker = _worker;
        }
    }

    if (worker) { xTaskNotifyGive(worker); }
}

void Provider::workerEntry(void* context)
{
    static_cast<Provider*>(context)->worker();
}

bool Provider::commandIoAuthorized(void* context)
{
    auto& io = *static_cast<CommandIoContext*>(context);
    return io.provider && io.provider->commandAuthorized(io.generation);
}

Modbus::Result Provider::commandIoWrite(void* context, uint16_t address,
        uint16_t count, uint16_t const* values)
{
    auto& io = *static_cast<CommandIoContext*>(context);
    if (!io.provider || !io.provider->commandAuthorized(io.generation)) {
        return { Modbus::Error::InvalidData };
    }
    return io.provider->_client.writeHoldingRegisters(address, count, values);
}

Modbus::Result Provider::commandIoRead(void* context, uint16_t address,
        uint16_t count, uint8_t* payload)
{
    auto& io = *static_cast<CommandIoContext*>(context);
    if (!io.provider) { return { Modbus::Error::InvalidData }; }
    return io.provider->_client.readHoldingRegisters(address, count, payload);
}

bool Provider::cleanupIoAuthorized(void* context)
{
    auto& io = *static_cast<CleanupIoContext*>(context);
    return io.provider && io.provider->cleanupAuthorized(io.token);
}

Modbus::Result Provider::cleanupIoWrite(void* context, uint16_t address,
        uint16_t count, uint16_t const* values)
{
    auto& io = *static_cast<CleanupIoContext*>(context);
    if (!io.provider || !io.provider->cleanupAuthorized(io.token)) {
        return { Modbus::Error::InvalidData };
    }
    return io.provider->_client.writeHoldingRegisters(address, count, values);
}

Modbus::Result Provider::cleanupIoRead(void* context, uint16_t address,
        uint16_t count, uint8_t* payload)
{
    auto& io = *static_cast<CleanupIoContext*>(context);
    if (!io.provider || !io.provider->cleanupAuthorized(io.token)) {
        return { Modbus::Error::InvalidData };
    }
    return io.provider->_client.readHoldingRegisters(address, count, payload);
}

void Provider::processCommand(Actuation::Envelope const& envelope)
{
    WarpConfig config;
    std::shared_ptr<Profiles::Profile const> profile;
    uint8_t unit = 1;
    uint32_t generation = envelope.generation;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_enabled && !_cleanup.valid && generation != 0 && generation == _generation) {
            config = _config;
            profile = _profile;
            unit = _unit;
        }
    }

    bool success = false;
    if (profile && config.Port && commandAuthorized(generation) && NetworkSettings.isConnected()) {
        if (_actuatorGeneration != generation) {
            _actuator.reset();
            _actuatorGeneration = generation;
        }

        auto open = _client.open(config.Host, config.Port, unit);
        if (open && commandAuthorized(generation)) {
            CommandIoContext context { this, generation };
            Actuation::RegisterIo io {
                &context,
                commandIoWrite,
                commandIoRead,
                commandIoAuthorized,
            };

            Actuation::CommandResult result;
            bool handled = true;
            switch (envelope.command.action) {
                case Actuation::Action::SetCurrentA:
                    result = _actuator.setCurrent(io, *profile, envelope.command.value);
                    break;
                case Actuation::Action::Stop:
                    result = _actuator.stop(io, *profile);
                    break;
                case Actuation::Action::Release:
                    result = _actuator.releaseControl(io, *profile);
                    break;
                default:
                    handled = false;
                    break;
            }
            success = handled && static_cast<bool>(result);
        }
    }
    _client.close();
    _mailbox.complete(envelope.id, success);

    bool ownsControl = _actuator.ownsControl();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _busy = false;
        if (generation == _generation) {
            _controlOwned = ownsControl;
            _polled = false;
        } else if (ownsControl && !_cleanup.valid && profile && config.Port) {
            retainCleanupLocked(config, profile, unit);
            _controlOwned = false;
        }
    }

    ESP_LOGI(TAG, "Control command %lu (%s) %s", static_cast<unsigned long>(envelope.id),
            Actuation::actionName(envelope.command.action), success ? "succeeded" : "failed");
}

void Provider::processCleanup()
{
    CleanupContext cleanup;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_cleanup.valid || _cleanupState != Actuation::CleanupState::Running) {
            _busy = false;
            return;
        }
        cleanup = _cleanup;
        ++_cleanupAttempts;
        _cleanupLastAttemptMs = millis();
    }

    Actuation::CommandResult result;
    if (!NetworkSettings.isConnected()) {
        result = cleanupTransportFailure({ Modbus::Error::Network });
    } else {
        auto open = _client.open(cleanup.config.Host, cleanup.config.Port, cleanup.unit);
        if (!open) {
            result = cleanupTransportFailure(open);
        } else if (cleanupAuthorized(cleanup.token)) {
            CleanupIoContext context { this, cleanup.token };
            Actuation::RegisterIo io {
                &context,
                cleanupIoWrite,
                cleanupIoRead,
                cleanupIoAuthorized,
            };
            result = _actuator.releaseRetainedControl(io, *cleanup.profile);
        } else {
            result.error = Actuation::CommandError::ExecutionFailed;
            result.stage = Actuation::CommandStage::Release;
            result.execution.error = Actuation::ExecutionError::AuthorizationRevoked;
        }
    }
    _client.close();

    bool success = static_cast<bool>(result);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _busy = false;
        if (!_cleanup.valid || cleanup.token != _cleanup.token) { return; }
        _cleanupLastResult = result;
        if (success) {
            _cleanup.valid = false;
            _cleanup.profile.reset();
            _cleanupState = Actuation::CleanupState::Succeeded;
            _controlOwned = false;
            _polled = false;
        } else {
            _cleanupState = Actuation::CleanupState::RetryWait;
        }
    }

    ESP_LOGI(TAG, "Retained control cleanup %s after %lu attempt(s)",
            success ? "succeeded" : "failed",
            static_cast<unsigned long>(_cleanupAttempts));
}

void Provider::worker()
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        Actuation::Envelope command;
        if (_mailbox.take(command)) {
            processCommand(command);
            continue;
        }

        bool cleanupWork = false;
        WarpConfig config;
        std::shared_ptr<Profiles::Profile const> profile;
        uint8_t unit = 1;
        uint32_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_busy) { continue; }
            cleanupWork = _cleanup.valid && _cleanupState == Actuation::CleanupState::Running;
            if (!cleanupWork) {
                if (!_enabled) { _busy = false; continue; }
                config = _config;
                profile = _profile;
                unit = _unit;
                generation = _generation;
            }
        }

        if (cleanupWork) {
            processCleanup();
            continue;
        }

        Sample sample;
        Modbus::Result result { Modbus::Error::Network };
        if (NetworkSettings.isConnected()) {
            result = _client.open(config.Host, config.Port, unit);
            if (result) {
                bool current;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    current = _enabled && generation == _generation;
                }
                if (current) {
                    if (profile) {
                        Evse::Sample profileSample;
                        result = Evse::poll(_client, *profile, profileSample);
                        if (result && !fromProfileSample(profileSample, sample)) {
                            result = { Modbus::Error::InvalidData, 0, true };
                        }
                    } else {
                        result = poll(_client, sample);
                    }
                }
            }
        }
        _client.close();
        bool available = false;
        bool changedError = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _busy = false;
            if (!_enabled || generation != _generation) { continue; }
            _lastPollMs = millis();
            auto previous = _state.snapshot(millis());
            _state.apply(sample, result, millis());
            available = result && !previous.valid;
            changedError = !result && (previous.error != result.error || previous.exception != result.exception);
        }
        if (available) {
            ESP_LOGI(TAG, "Read-only telemetry available/recovered (%s)",
                    profile ? "imported profile" : "legacy WARP mapping");
        } else if (changedError) {
            ESP_LOGW(TAG, "Read-only poll failed: %s (Modbus exception %u)",
                    Modbus::errorName(result.error), result.exception);
        }
    }
}

} // namespace Hems::Warp
