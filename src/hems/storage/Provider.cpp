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

void Provider::loop()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled || _busy || (_polled && millis() - _lastPollMs < PollIntervalMs)) { return; }
    if (!_worker && xTaskCreate(workerEntry, "HEMS:Storage", 4096, this, 1, &_worker) != pdPASS) {
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

void Provider::worker()
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        StorageConfig config;
        std::shared_ptr<Profiles::Profile const> profile;
        uint16_t port;
        uint8_t unit;
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(_mutex);
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
