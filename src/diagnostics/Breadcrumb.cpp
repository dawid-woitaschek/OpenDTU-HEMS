// SPDX-License-Identifier: GPL-2.0-or-later
#include <diagnostics/Breadcrumb.h>
#include <cstring>
namespace Diagnostics {
namespace {
constexpr uint32_t Magic = 0x48454d44;
uint32_t checksum(Record const& r)
{
    uint32_t hash = 2166136261U;
    auto bytes = reinterpret_cast<uint8_t const*>(&r);
    for (size_t i = 0; i < offsetof(Record, checksum); ++i) { hash = (hash ^ bytes[i]) * 16777619U; }
    return hash;
}
unsigned lane(Component c)
{
    switch (c) {
    case Component::Energy: case Component::Optimizer: return 0; // One shared serial worker.
    case Component::FirmwareUpdate: return 1;
    case Component::Configuration: return 2;
    case Component::Network: return 3;
    case Component::Maintenance: return 4;
    default: return 5;
    }
}
} // namespace
bool valid(Record const& r)
{
    if (r.magic != Magic || r.version != 1 || r.size != sizeof(Record) || !r.generation || !r.sequence
        || r.restartSource >= RestartSource::Count || r.restartRequested > 1 || r.intentionalRestart > 1 || r.espRestartCalled > 1 || r.shutdownRegistered > 1
        || checksum(r) != r.checksum) { return false; }
    for (auto const& a : r.activities) {
        if (a.component >= Component::Count || a.operation >= Operation::Count || a.phase >= Phase::Count || a.active > 1) { return false; }
    }
    return true;
}
Activity const* selected(Record const& r)
{
    Activity const* result = nullptr;
    for (auto const& a : r.activities) {
        if (!a.sequence) { continue; }
        if (!result || (a.active && !result->active) || (a.active == result->active && static_cast<int32_t>(a.sequence - result->sequence) > 0)) { result = &a; }
    }
    return result;
}
void Journal::boot(Storage& storage, bool retain)
{
    _storage = &storage; previousValid = false;
    bool a = retain && valid(storage.slots[0]), b = retain && valid(storage.slots[1]);
    _slot = b && (!a || static_cast<int32_t>(storage.slots[1].sequence - storage.slots[0].sequence) > 0) ? 1 : 0;
    if (a || b) { previous = storage.slots[_slot]; previousValid = true; }
    else { previous = {}; }
    current = {}; current.magic = Magic; current.version = 1; current.size = sizeof(Record);
    current.generation = previousValid ? previous.generation + 1 : 1;
    if (!current.generation) { current.generation = 1; }
    current.sequence = previousValid ? previous.sequence : 0;
    next(); commit();
}
uint32_t Journal::next() { if (!++current.sequence) { ++current.sequence; } return current.sequence; }
void Journal::commit()
{
    if (!_storage) { return; }
    current.checksum = checksum(current);
    _slot ^= 1;
    auto& target = _storage->slots[_slot]; target.magic = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memcpy(reinterpret_cast<uint8_t*>(&target) + sizeof(target.magic), reinterpret_cast<uint8_t const*>(&current) + sizeof(current.magic), sizeof(Record) - sizeof(current.magic));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    target.magic = Magic; // Commit last; a torn write leaves the other valid slot.
}
uint32_t Journal::begin(Component c, Operation op, Phase p, int8_t index, uint32_t ms)
{
    auto token = next();
    current.activities[lane(c)] = { token, ms, c, op, p, index, 1, {} }; commit(); return token;
}
void Journal::phase(uint32_t token, Phase p, int8_t index, uint32_t ms)
{
    for (auto& a : current.activities) {
        if (a.sequence != token || !a.active) { continue; }
        a.phase = p; if (index >= 0) { a.subIndex = index; } a.uptimeMs = ms; next(); commit(); return;
    }
}
void Journal::finish(uint32_t token, uint32_t ms)
{
    for (auto& a : current.activities) {
        if (a.sequence != token || !a.active) { continue; }
        a.active = 0; a.phase = Phase::Complete; a.uptimeMs = ms; next(); commit(); return;
    }
}
void Journal::restartRequested(RestartSource source, uint32_t ms)
{
    current.restartRequested = 1; current.restartSource = source; current.restartRequestedAt = ms; next(); commit();
}
void Journal::intentionalRestart(uint32_t ms) { current.intentionalRestart = 1; current.restartAt = ms; next(); commit(); }
void Journal::hookRegistered(bool registered) { current.shutdownRegistered = registered; next(); commit(); }
void Journal::espRestart() { current.espRestartCalled = 1; next(); commit(); }
char const* name(Component v) { constexpr char const* names[] = { "none", "energy", "optimizer", "firmware_update", "configuration", "network", "maintenance", "runtime" }; return v < Component::Count ? names[unsigned(v)] : "unknown"; }
char const* name(Operation v) { constexpr char const* names[] = { "none", "tariff", "forecast", "calculate", "upload", "write", "reset", "restore", "apply", "reconnect", "boot", "restart" }; return v < Operation::Count ? names[unsigned(v)] : "unknown"; }
char const* name(Phase v) { constexpr char const* names[] = { "start", "provider_start", "dns", "connect", "tls_http", "decode", "aggregate", "configuration", "network", "complete" }; return v < Phase::Count ? names[unsigned(v)] : "unknown"; }
char const* name(RestartSource v) { constexpr char const* names[] = { "none", "maintenance_reboot", "firmware_update", "configuration_reset", "file_restore", "pin_mapping_change", "inverter_unresponsive", "unspecified" }; return v < RestartSource::Count ? names[unsigned(v)] : "unknown"; }
} // namespace Diagnostics
