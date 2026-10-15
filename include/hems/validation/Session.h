// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Hems::Validation {
constexpr uint32_t DefaultTtlMs = 120000;
constexpr uint32_t MaxTtlMs = 300000;
constexpr uint32_t MinCommandMs = 1000;
enum class State : uint8_t { Inactive, Active, Expired, Revoked };
struct Snapshot {
    State state = State::Inactive;
    bool active = false;
    uint32_t remainingMs = 0;
    uint32_t commandsAccepted = 0;
};
char const* stateName(State state);
class Session {
public:
    bool start(uint32_t now, uint32_t ttl, void (*random)(void*, size_t), char* token);
    bool expire(uint32_t now);
    bool revoke(char const* token = nullptr); // null: internal config/expiry revocation
    bool accept(char const* token, uint32_t now);
    Snapshot snapshot(uint32_t now) const;
private:
    mutable std::mutex _mutex;
    State _state = State::Inactive;
    uint32_t _started = 0, _ttl = 0, _lastCommand = 0, _commands = 0;
    char _token[33] {};
};
} // namespace Hems::Validation
