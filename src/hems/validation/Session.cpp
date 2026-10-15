// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/validation/Session.h>
#include <cstring>
namespace Hems::Validation {
bool Session::start(uint32_t now, uint32_t ttl, void (*random)(void*, size_t), char* token) {
    if (!random || !token || ttl < 1000 || ttl > MaxTtlMs) { return false; }
    uint8_t bytes[16]; random(bytes, sizeof(bytes));
    std::lock_guard<std::mutex> lock(_mutex);
    // An expired Active session must first be serviced, so restarting cannot
    // skip the release request associated with the previous session.
    if (_state == State::Active) { return false; }
    static constexpr char Hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i) { _token[i*2] = Hex[bytes[i] >> 4]; _token[i*2+1] = Hex[bytes[i] & 15]; }
    _token[32] = 0; memcpy(token, _token, sizeof(_token));
    _state = State::Active; _started = now; _ttl = ttl; _commands = 0; _lastCommand = 0;
    return true;
}
bool Session::expire(uint32_t now) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state != State::Active || now - _started < _ttl) { return false; }
    _state = State::Expired; _token[0] = 0; return true;
}
bool Session::revoke(char const* token) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state != State::Active || (token && (strlen(token) != 32 || strcmp(token, _token)))) { return false; }
    _state = State::Revoked; _token[0] = 0; return true;
}
bool Session::accept(char const* token, uint32_t now) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_state != State::Active || now - _started >= _ttl || !token || strlen(token) != 32 || strcmp(token, _token)
            || (_commands && now - _lastCommand < MinCommandMs)) { return false; }
    _lastCommand = now; ++_commands; return true;
}
Snapshot Session::snapshot(uint32_t now) const {
    std::lock_guard<std::mutex> lock(_mutex);
    bool active = _state == State::Active && now - _started < _ttl;
    return { _state == State::Active && !active ? State::Expired : _state, active,
        active ? _ttl - (now - _started) : 0, _commands };
}
char const* stateName(State state) {
    switch (state) {
        case State::Inactive: return "inactive";
        case State::Active: return "active";
        case State::Expired: return "expired";
        case State::Revoked: return "revoked";
    }
    return "unknown";
}
} // namespace Hems::Validation
