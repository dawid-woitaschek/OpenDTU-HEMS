// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/warp/Provider.h>
#include <hems/warp/Poller.h>
#include <NetworkSettings.h>
#include <esp_log.h>
#include <cstring>

#undef TAG
static const char* TAG = "hems-warp";

namespace Hems::Warp {

void Provider::configure(WarpConfig const& config, bool monitor)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _config = config;
    _enabled = monitor && config.Enabled && config.Host[0] != '\0' && config.Port != 0;
    ++_generation;
    _polled = false;
    _state.reset(config.Enabled, _enabled);
}

Snapshot Provider::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _state.snapshot(millis());
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
        uint32_t generation;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!_enabled) { _busy = false; continue; }
            config = _config;
            generation = _generation;
        }
        Sample sample;
        Modbus::Result result { Modbus::Error::Network };
        if (NetworkSettings.isConnected()) {
            result = _client.open(config.Host, config.Port);
            if (result) {
                // Recheck configuration after DNS/connect before emitting a read.
                bool current;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    current = _enabled && generation == _generation;
                }
                if (current) { result = poll(_client, sample); }
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
            ESP_LOGI(TAG, "Read-only telemetry available/recovered");
        } else if (changedError) {
            ESP_LOGW(TAG, "Read-only poll failed: %s (Modbus exception %u)",
                    Modbus::errorName(result.error), result.exception);
        }
    }
}

} // namespace Hems::Warp
