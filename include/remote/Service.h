// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <remote/Core.h>
#include <remote/Schedule.h>
#include <mutex>
namespace Remote {
class Service {
public:
    void init();
    void configuration(JsonObject out) const;
    bool configure(JsonVariantConst input);
    void status(JsonObject out) const;
    bool requestSend();
    bool due(); // Scheduler: scalar snapshots only; no TLS/JSON.
    void run(); // Existing shared HEMS background worker only.
private:
    bool buildPayload(uint8_t triggers);
    void expireLocked(uint32_t now);
    bool load();
    char const* prerequisite(Settings const& config) const;
    mutable std::mutex _mutex;
    Settings _settings;
    Transmission _flight;
    Schedule _schedule;
    uint8_t _lastHemsTriggers = 0;
    uint32_t _expiredTransmissions = 0, _droppedTelemetry = 0, _lastHemsUtc = 0, _lastHemsMs = 0;
    char* _body = nullptr; // Strictly PSRAM, immutable through all retries.
    size_t _bodySize = 0;
    bool _busy = false, _previousSent = false, _attempted = false, _succeeded = false;
    uint32_t _earliestMs = 0, _lastAttemptMs = 0, _lastSuccessMs = 0, _seenFailure = 0;
    unsigned _failures = 0;
    int _httpResult = 0;
    char const* _error = nullptr;
};
extern Service Sender;
bool post(Settings const& settings, char const* body, size_t length, Transmission const& flight, int& result, char const*& error);
} // namespace Remote
