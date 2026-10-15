// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Provider.h>
#include <hems/storage/Poller.h>
#include <hems/profiles/FlashStore.h>
#include <NetworkSettings.h>
#include <Arduino.h>
#include <esp_log.h>
#include <new>

#undef TAG
static const char* TAG = "hems-storage";

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

namespace Hems::Storage {

void Provider::retainCleanupLocked(StorageConfig const& config,
        std::shared_ptr<Profiles::Profile const> const& profile,
        uint16_t port, uint8_t unit)
{
    if (_cleanup.valid || !profile || !config.Host[0] || !port || !unit) { return; }

    _cleanup.valid = true;
    _cleanup.config = config;
    _cleanup.profile = profile;
    _cleanup.port = port;
    _cleanup.unit = unit;
    _cleanup.token = _nextCleanupToken++;
    if (_cleanup.token == 0) { _cleanup.token = _nextCleanupToken++; }
    if (_nextCleanupToken == 0) { _nextCleanupToken = 1; }
    _cleanupState = Actuation::CleanupState::Pending;
    _cleanupAttempts = 0;
    _cleanupLastAttemptMs = 0;
    _cleanupLastResult = {};
}

void Provider::configure(StorageConfig const& config, bool monitor)
{
    std::shared_ptr<Profiles::Profile const> profile;
    bool loaded = false;
    if (config.Enabled && config.Profile[0]) {
        auto candidate = std::shared_ptr<Profiles::Profile>(new (std::nothrow) Profiles::Profile());
        if (candidate) {
            bool builtIn = false;
            char const* error = nullptr;
            std::lock_guard<std::mutex> storeLock(Profiles::runtimeStoreMutex());
            bool found = Profiles::runtimeStore().get(config.Profile, *candidate, builtIn, error);
            loaded = found && supportsProfile(*candidate);
            if (loaded) { profile = std::move(candidate); }
        }
    }

    uint16_t port = profile ? (config.Port ? config.Port : profile->port) : 0;
    uint8_t unit = profile ? (config.Unit ? config.Unit : profile->unit) : 0;

    // Pending work belongs to the old endpoint/profile generation and must not
    // survive a configuration change. Running work is stopped by the per-write
    // generation gate. If it already took ownership, processCommand() retains
    // its local old context after the generation mismatch is observed.
    _mailbox.cancelPending();

    std::lock_guard<std::mutex> lock(_mutex);
    if (_controlOwned && !_cleanup.valid) {
        retainCleanupLocked(_config, _profile, _port, _unit);
    }
    _controlOwned = false;

    _config = config;
    _profile = std::move(profile);
    _port = port;
    _unit = unit;
    _enabled = monitor && config.Enabled && loaded && config.Host[0] && port && unit;
    ++_generation;
    _polled = false;
    _state.reset(config.Enabled, _enabled, loaded, port, unit);
}

Snapshot Provider::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _state.snapshot(millis());
}

bool Provider::controlCapable() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _profile && Profiles::hasControl(*_profile) && _profile->releaseWriteCount != 0;
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
    if (!callback || !callback(context, ControlTarget::Storage)) { return false; }
    // The callback runs without our mutex and can race with reconfiguration.
    // Its approval must still refer to the generation checked above.
    std::lock_guard<std::mutex> lock(_mutex);
    return _enabled && _profile && !_cleanup.valid && generation == _generation;
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
    return xTaskCreate(workerEntry, "HEMS:Storage", 4096, this, 1, &_worker) == pdPASS;
}

bool Provider::submitCommand(Actuation::Command const& command, uint32_t& id)
{
    id = 0;
    if (command.target != ControlTarget::Storage || !Actuation::validCommand(command)) { return false; }

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
    StorageConfig config;
    std::shared_ptr<Profiles::Profile const> profile;
    uint16_t port = 0;
    uint8_t unit = 0;
    uint32_t generation = envelope.generation;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_enabled && !_cleanup.valid && generation != 0 && generation == _generation) {
            config = _config;
            profile = _profile;
            port = _port;
            unit = _unit;
        }
    }

    bool success = false;
    if (profile && port && unit && commandAuthorized(generation) && NetworkSettings.isConnected()) {
        if (_actuatorGeneration != generation) {
            _actuator.reset();
            _actuatorGeneration = generation;
        }

        auto open = _client.open(config.Host, port, unit);
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
                case Actuation::Action::SetChargePowerW:
                    result = _actuator.setChargePower(io, *profile, envelope.command.value);
                    break;
                case Actuation::Action::SetDischargePowerW:
                    result = _actuator.setDischargePower(io, *profile, envelope.command.value);
                    break;
                case Actuation::Action::SetTargetSocPct:
                    result = _actuator.setTargetSoc(io, *profile, envelope.command.value);
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

    bool ownsControl = _actuator.ownsControl();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _busy = false;
        if (generation == _generation) {
            _controlOwned = ownsControl;
            _polled = false;
        } else if (ownsControl && !_cleanup.valid && profile && port && unit) {
            retainCleanupLocked(config, profile, port, unit);
            _controlOwned = false;
        }
    }

    // Publish completion only after ownership/cleanup and dispatch state agree.
    _mailbox.complete(envelope.id, success);

    ESP_LOGI(TAG, "Control command %lu (%s) %s", static_cast<unsigned long>(envelope.id),
            Actuation::actionName(envelope.command.action), success ? "succeeded" : "failed");
}

void Provider::processCleanup()
{
    CleanupContext cleanup;
    uint32_t attempts = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_cleanup.valid || _cleanupState != Actuation::CleanupState::Running) {
            _busy = false;
            return;
        }
        cleanup = _cleanup;
        attempts = ++_cleanupAttempts;
        _cleanupLastAttemptMs = millis();
    }

    Actuation::CommandResult result;
    if (!NetworkSettings.isConnected()) {
        result = cleanupTransportFailure({ Modbus::Error::Network });
    } else {
        auto open = _client.open(cleanup.config.Host, cleanup.port, cleanup.unit);
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
            _cleanupLastAttemptMs = millis();
            _cleanupState = Actuation::CleanupState::RetryWait;
        }
    }

    ESP_LOGI(TAG, "Retained control cleanup %s after %lu attempt(s)",
            success ? "succeeded" : "failed",
            static_cast<unsigned long>(attempts));
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
        StorageConfig config;
        std::shared_ptr<Profiles::Profile const> profile;
        uint16_t port = 0;
        uint8_t unit = 0;
        uint32_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_busy) { continue; }
            cleanupWork = _cleanup.valid && _cleanupState == Actuation::CleanupState::Running;
            if (!cleanupWork) {
                if (!_enabled) { _busy = false; continue; }
                config = _config;
                profile = _profile;
                port = _port;
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
        if (profile && NetworkSettings.isConnected()) {
            result = _client.open(config.Host, port, unit);
            if (result) {
                bool current;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    current = _enabled && generation == _generation;
                }
                if (current) { result = poll(_client, *profile, sample); }
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
            ESP_LOGI(TAG, "Read-only storage telemetry available/recovered");
        } else if (changedError) {
            ESP_LOGW(TAG, "Read-only storage poll failed: %s (Modbus exception %u)",
                    Modbus::errorName(result.error), result.exception);
        }
    }
}

} // namespace Hems::Storage
