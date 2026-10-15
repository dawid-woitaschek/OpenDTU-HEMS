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

namespace Hems::Storage {

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
    // survive a configuration change. A running command cannot be cancelled;
    // its per-write authorization/generation guard will stop subsequent writes.
    _mailbox.cancelPending();

    std::lock_guard<std::mutex> lock(_mutex);
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
    return _profile && Profiles::hasControl(*_profile);
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
        if (!_enabled || !_profile || generation != _generation) { return false; }
        context = _authorizationContext;
        callback = _authorization;
    }
    return callback && callback(context, ControlTarget::Storage);
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
        if (!_enabled || !_profile) { return false; }
        generation = _generation;
    }
    if (!commandAuthorized(generation)) { return false; }
    if (!_mailbox.submit(command, id)) { return false; }

    TaskHandle_t worker = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (generation == _generation && ensureWorkerLocked()) { worker = _worker; }
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

    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled || _busy || (_polled && millis() - _lastPollMs < PollIntervalMs)) { return; }
    if (!ensureWorkerLocked()) {
        _state.apply({}, { Modbus::Error::TaskUnavailable }, millis());
        _polled = true;
        _lastPollMs = millis();
        return;
    }
    _busy = true;
    _polled = true;
    _lastPollMs = millis();
    xTaskNotifyGive(_worker);
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
    // Readback remains safe after a gate change and is useful to determine the
    // state left by the last completed write. Only writes are authorization gated.
    return io.provider->_client.readHoldingRegisters(address, count, payload);
}

void Provider::processCommand(Actuation::Envelope const& envelope)
{
    StorageConfig config;
    std::shared_ptr<Profiles::Profile const> profile;
    uint16_t port = 0;
    uint8_t unit = 0;
    uint32_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_enabled) {
            config = _config;
            profile = _profile;
            port = _port;
            unit = _unit;
            generation = _generation;
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
    _mailbox.complete(envelope.id, success);

    {
        std::lock_guard<std::mutex> lock(_mutex);
        // A command may have consumed a wake-up originally scheduled for a poll.
        // Never leave the provider permanently busy in that race.
        _busy = false;
        if (generation == _generation) {
            // Follow a command with a fresh telemetry poll instead of waiting for
            // the old interval, so status reflects the resulting device state.
            _polled = false;
        }
    }

    ESP_LOGI(TAG, "Control command %lu (%s) %s", static_cast<unsigned long>(envelope.id),
            Actuation::actionName(envelope.command.action), success ? "succeeded" : "failed");
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

        StorageConfig config;
        std::shared_ptr<Profiles::Profile const> profile;
        uint16_t port;
        uint8_t unit;
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            // A cancelled/stale command can leave a task notification behind.
            // Only the scheduler marks a telemetry poll by setting _busy first.
            if (!_busy) { continue; }
            if (!_enabled) { _busy = false; continue; }
            config = _config;
            profile = _profile;
            port = _port;
            unit = _unit;
            generation = _generation;
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
