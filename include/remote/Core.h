// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <diagnostics/Heap.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <type_traits>

namespace Remote {
constexpr size_t MaxPayload = 192 * 1024; // Well below the collector's 1 MiB limit.
constexpr uint32_t RetentionMs = 60U * 60 * 1000;
constexpr size_t SampleCapacity = 4096, FailureCapacity = 64, BatchSamples = 128;
struct Settings {
    bool enabled = false, quarterHour = false;
    uint16_t intervalMinutes = 60;
    char endpoint[257] {}, token[129] {};
};
bool validEndpoint(char const* url);
bool validToken(char const* token);
bool parseSettings(JsonVariantConst in, Settings& settings, bool persisted = false);
void settingsJson(Settings const& settings, JsonObject out, bool persist = false);

// Caller owns storage and synchronizes short POD copies. No allocation in push.
template<class T> class Ring {
public:
    struct Entry { uint32_t sequence; T value; };
    void attach(Entry* entries, size_t capacity) { _entries = entries; _capacity = capacity; }
    bool available() const { return _entries && _capacity; }
    void push(uint32_t sequence, T const& value) {
        if (!available()) { return; }
        if (_count == _capacity) { ++_dropped; } else { ++_count; }
        _entries[_next] = { sequence, value };
        _next = (_next + 1) % _capacity; _newest = sequence;
    }
    bool read(uint32_t sequence, T& value) const {
        size_t low = 0, high = _count;
        while (low < high) {
            auto mid = low + (high - low) / 2;
            auto const& entry = _entries[(_next + _capacity - _count + mid) % _capacity];
            if (entry.sequence == sequence) { value = entry.value; return true; }
            if (entry.sequence < sequence) { low = mid + 1; } else { high = mid; }
        }
        return false;
    }
    uint32_t oldest() const { return _count ? _entries[(_next + _capacity - _count) % _capacity].sequence : 0; }
    void acknowledge(uint32_t sequence) {
        if (sequence < _acked || sequence > _newest) { return; }
        while (_count && oldest() <= sequence) { --_count; }
        _acked = sequence;
    }
    // Local retention is deliberately separate from collector acknowledgement.
    size_t expire(uint32_t now, uint32_t age, size_t budget = 16) {
        size_t removed = 0;
        while (_count && removed < budget) {
            auto const& entry = _entries[(_next + _capacity - _count) % _capacity];
            if (now - entry.value.uptimeMs < age) { break; }
            --_count; ++removed; ++_expired;
        }
        return removed;
    }
    uint32_t expired() const { return _expired; }
    size_t count() const { return _count; }
    uint32_t newest() const { return _newest; }
    uint32_t acknowledged() const { return _acked; }
    uint32_t dropped() const { return _dropped; }
    void lost(uint32_t count) { _dropped += count; }
private:
    Entry* _entries = nullptr;
    size_t _capacity = 0, _count = 0, _next = 0;
    uint32_t _newest = 0, _acked = 0, _dropped = 0, _expired = 0;
};
struct Transmission {
    uint32_t sequence = 1, checkpoint = 0, failure = 0, stagedMs = 0;
    bool pending = false;
    void stage(uint32_t highest, uint32_t failures, uint32_t now = 0) { checkpoint = highest; failure = failures; stagedMs = now; pending = true; }
    bool expired(uint32_t now) const { return pending && now - stagedMs >= RetentionMs; }
    void abandon() { if (pending) { pending = false; ++sequence; } }
    bool accepts(JsonVariantConst ack) const {
        return pending && ack.is<JsonObjectConst>() && ack["accepted"].is<bool>() && ack["accepted"].as<bool>()
            && ack["accepted_checkpoint_sequence"].is<uint32_t>()
            && ack["accepted_checkpoint_sequence"].as<uint32_t>() == checkpoint;
    }
    void complete() { pending = false; ++sequence; }
};
inline uint32_t retrySeconds(unsigned failures) { return 60U << (failures > 5 ? 5 : failures); } // 1–32 minutes
inline void bootId(uint8_t const (&random)[16], char (&out)[33]) {
    for (size_t i = 0; i < 16; ++i) {
        snprintf(out + 2 * i, 3, "%02x", random[i]);  // NOLINT(runtime/printf): one byte, two digits plus NUL.
    }
}
// RTC uses a byte array, never a C++ dynamically initialized retained object.
struct Capsule {
    uint32_t magic, version, size, checkpoint, failureCount, intentionalRestart;
    char boot[33];
    uint8_t reserved[3];
    Diagnostics::HeapSample sample;
    Diagnostics::HeapFailure failure;
    uint32_t checksum;
};
static_assert(std::is_trivially_copyable<Capsule>::value && sizeof(Capsule) < 256);
inline uint32_t capsuleChecksum(Capsule const& c) {
    uint32_t hash = 2166136261U;
    auto bytes = reinterpret_cast<uint8_t const*>(&c);
    for (size_t i = 0; i < offsetof(Capsule, checksum); ++i) { hash = (hash ^ bytes[i]) * 16777619U; }
    return hash;
}
inline void seal(Capsule& c) { c.magic = 0x52444331; c.version = 1; c.size = sizeof(c); c.checksum = capsuleChecksum(c); }
inline bool valid(Capsule const& c) {
    return c.magic == 0x52444331 && c.version == 1 && c.size == sizeof(c) && c.boot[32] == 0
        && c.intentionalRestart <= 1 && c.sample.context.component < Diagnostics::Component::Count
        && c.checksum == capsuleChecksum(c);
}
} // namespace Remote
