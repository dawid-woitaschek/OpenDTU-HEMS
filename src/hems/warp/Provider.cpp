// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/warp/Provider.h>
#include <hems/evse/Poller.h>
#include <hems/profiles/FlashStore.h>
#include <hems/warp/Poller.h>
#include <hems/warp/ProfileAdapter.h>
#include <NetworkSettings.h>
#include <esp_log.h>
#include <cstring>
#include <new>

#undef TAG
static const char* TAG = "hems-warp";

namespace Hems::Warp {

void Provider::configure(WarpConfig const& config, bool monitor)
{
    std::shared_ptr<Profiles::Profile const> profile;
    auto candidate = std::shared_ptr<Profiles::Profile>(new (std::nothrow) Profiles::Profile());
    if (candidate) {
        bool builtIn = false;
        char const* error = nullptr;
        std::lock_guard<std::mutex> storeLock(Profiles::runtimeStoreMutex());
        bool found = Profiles::runtimeStore().get(RepositoryProfileId, *candidate, builtIn, error);
        if (found && !builtIn && Evse::supportsProfile(*candidate)) {
            profile = std::move(candidate);
        }
    }

    uint8_t unit = profile ? profile->unit : 1;

    std::lock_guard<std::mutex> lock(_mutex);
    _config = config;
    _profile = std::move(profile);
    _unit = unit;
    _enabled = monitor && config.Enabled && config.Host[0] != '\0' && config.Port != 0;
    ++_generation;
    _polled = false;
    _state.reset(config.Enabled, _enabled);
}

Snapshot Provider::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto result = _state.snapshot(millis());
    result.profileDriven = static_cast<bool>(_profile);
    return result;
}

void Provider::loop()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_enabled || _busy || (_polled && millis() - _lastPollMs < PollIntervalMs)) { return; }
    // Allocate one small worker only on first use; it sleeps when disabled.
    // Priority 1, 4096-byte internal stack, no PSRAM dependency.
    if (!_worker && xTaskCreate(workerEntry, "HEMS:WARP", 4096, this, 1, &_worker) != pdPASS) {
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
        WarpConfig config;
        std::shared_ptr<Profiles::Profile const> profile;
        uint8_t unit;
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_enabled) { _busy = false; continue; }
            config = _config;
            profile = _profile;
            unit = _unit;
            generation = _generation;
        }
        Sample sample;
        Modbus::Result result { Modbus::Error::Network };
        if (NetworkSettings.isConnected()) {
            result = _client.open(config.Host, config.Port, unit);
            if (result) {
                // Recheck configuration after DNS/connect before emitting a read.
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
            // Back off after completion too, including after slow failures.
            _lastPollMs = millis();
            auto previous = _state.snapshot(millis());
            _state.apply(sample, result, millis());
            available = result && !previous.valid;
            changedError = !result && (previous.error != result.error || previous.exception != result.exception);
        }
        // Logging can wait for a sink; keep it outside the snapshot mutex too.
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
