// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <remote/Core.h>
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
    bool buildPayload();
    bool load();
    char const* prerequisite(Settings const& config) const;
    mutable std::mutex _mutex;
    Settings _settings;
    Transmission _flight;
    char* _body = nullptr; // Strictly PSRAM, immutable through all retries.
    size_t _bodySize = 0;
    bool _busy = false, _manual = false, _previousSent = false, _attempted = false, _succeeded = false;
    uint32_t _nextMs = 0, _earliestMs = 0, _lastAttemptMs = 0, _lastSuccessMs = 0, _seenFailure = 0;
    unsigned _failures = 0;
    int _httpResult = 0;
    char const* _error = nullptr;
};
extern Service Sender;
bool post(Settings const& settings, char const* body, size_t length, Transmission const& flight, int& result, char const*& error);
} // namespace Remote
