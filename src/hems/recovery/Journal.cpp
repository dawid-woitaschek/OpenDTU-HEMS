// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/recovery/Journal.h>
#include <memory>
#include <new>
#include <cstring>

namespace Hems::Recovery {
namespace {
constexpr uint8_t Version = 1;
// Fixed, explicitly encoded wire layout: magic/version/target/owned, endpoint,
// profile, diagnostic generation, exact recovery semantics, identity, CRC32.
constexpr size_t WireSize = 4 + 1 + 1 + 1 + 254 + 2 + 1 + 49 + 4 + 1 + MaxCompatibility + 1 + Profiles::MaxIdentityBytes + 4;
static_assert(WireSize <= MaxRecord);
void put16(uint8_t*& p, uint16_t n) { *p++ = n >> 8; *p++ = n; }
void put32(uint8_t*& p, uint32_t n) { put16(p, n >> 16); put16(p, n); }
uint16_t get16(uint8_t const*& p) { auto n = uint16_t(p[0]) << 8 | p[1]; p += 2; return n; }
uint32_t get32(uint8_t const*& p) { uint32_t n = get16(p); return n << 16 | get16(p); }
bool endpointEqual(Endpoint const& a, Endpoint const& b) {
    return a.port == b.port && a.unit == b.unit && !strcmp(a.host, b.host);
}
bool validEndpoint(Endpoint const& e) {
    return memchr(e.host, 0, sizeof(e.host)) && e.host[0] && e.port && e.unit && e.unit <= 247;
}
bool paddingZero(uint8_t const* bytes, size_t first, size_t end) {
    for (size_t i = first; i < end; ++i) { if (bytes[i]) { return false; } }
    return true;
}
bool matching(Record const& r, Profiles::Profile const& p) {
    uint8_t bytes[MaxCompatibility] {}, size = 0;
    return !strcmp(r.profile, p.id) && compatibility(p, bytes, size)
        && size == r.compatibilitySize && !memcmp(bytes, r.compatibility, size);
}
}
uint32_t checksum(uint8_t const* bytes, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) { crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
    }
    return ~crc;
}
bool encode(Record const& r, uint8_t* data, size_t& size) {
    if (!data || (r.target != ControlTarget::Evse && r.target != ControlTarget::Storage)
            || r.compatibilitySize > MaxCompatibility || r.identity.size > Profiles::MaxIdentityBytes
            || (r.possibleOwnership && (!validEndpoint(r.endpoint) || !Profiles::validId(r.profile)))) { return false; }
    memset(data, 0, WireSize);
    auto p = data;
    memcpy(p, "HEMR", 4); p += 4; *p++ = Version;
    *p++ = static_cast<uint8_t>(r.target); *p++ = r.possibleOwnership;
    memcpy(p, r.endpoint.host, sizeof(r.endpoint.host)); p += sizeof(r.endpoint.host);
    put16(p, r.endpoint.port); *p++ = r.endpoint.unit;
    memcpy(p, r.profile, sizeof(r.profile)); p += sizeof(r.profile);
    put32(p, r.generation); *p++ = r.compatibilitySize;
    memcpy(p, r.compatibility, r.compatibilitySize); p += MaxCompatibility;
    *p++ = r.identity.size; memcpy(p, r.identity.bytes, r.identity.size); p += Profiles::MaxIdentityBytes;
    put32(p, checksum(data, WireSize - 4));
    size = WireSize; return true;
}
Reason decode(uint8_t const* data, size_t size, ControlTarget target, Record& r) {
    r = {};
    if (!data || size != WireSize || memcmp(data, "HEMR", 4)) { return Reason::JournalCorrupt; }
    if (data[4] != Version) { return Reason::JournalVersion; }
    auto crc = data + size - 4;
    if (get32(crc) != checksum(data, size - 4) || data[5] != static_cast<uint8_t>(target) || data[6] > 1) { return Reason::JournalCorrupt; }
    auto p = data + 7; r.target = target; r.possibleOwnership = data[6];
    memcpy(r.endpoint.host, p, sizeof(r.endpoint.host)); p += sizeof(r.endpoint.host);
    r.endpoint.port = get16(p); r.endpoint.unit = *p++;
    memcpy(r.profile, p, sizeof(r.profile)); p += sizeof(r.profile);
    r.generation = get32(p); r.compatibilitySize = *p++;
    if (r.compatibilitySize > MaxCompatibility || !paddingZero(p, r.compatibilitySize, MaxCompatibility)) { return Reason::JournalCorrupt; }
    memcpy(r.compatibility, p, MaxCompatibility); p += MaxCompatibility;
    r.identity.size = *p++;
    if (r.identity.size > Profiles::MaxIdentityBytes || !paddingZero(p, r.identity.size, Profiles::MaxIdentityBytes)) { return Reason::JournalCorrupt; }
    memcpy(r.identity.bytes, p, Profiles::MaxIdentityBytes);
    if (!memchr(r.profile, 0, sizeof(r.profile)) || !memchr(r.endpoint.host, 0, sizeof(r.endpoint.host))
            || (r.possibleOwnership && (!validEndpoint(r.endpoint) || !Profiles::validId(r.profile) || !r.compatibilitySize))) { return Reason::JournalCorrupt; }
    // Cleared records are exact tombstones; corruption cannot manufacture idle.
    if (!r.possibleOwnership && !paddingZero(data, 7, size - 4)) { return Reason::JournalCorrupt; }
    return Reason::None;
}
bool compatibility(Profiles::Profile const& profile, uint8_t* data, uint8_t& size) {
    Profiles::ControlPlan release;
    if (!Profiles::planRelease(profile, release) || profile.identityFieldCount > Profiles::MaxIdentityFields) { return false; }
    auto p = data;
    *p++ = profile.schemaVersion; *p++ = profile.capabilities; *p++ = profile.releaseReplaySafe;
    *p++ = release.count;
    for (size_t i = 0; i < release.count; ++i) {
        auto const& w = release.writes[i];
        put16(p, w.address); *p++ = w.count;
        for (size_t j = 0; j < w.count; ++j) { put16(p, w.values[j]); }
    }
    *p++ = profile.identityFieldCount;
    for (size_t i = 0; i < profile.identityFieldCount; ++i) {
        auto const& f = profile.identityFields[i];
        if (!f.count || f.count > Profiles::MaxIdentityRegisters || uint32_t(f.address) + f.count > 65536
                || (f.space != Modbus::ReadFunction::InputRegisters && f.space != Modbus::ReadFunction::HoldingRegisters)) { return false; }
        *p++ = static_cast<uint8_t>(f.space); put16(p, f.address); *p++ = f.count;
    }
    size = p - data;
    return size <= MaxCompatibility;
}
bool automaticSupported(Profiles::Profile const& p) {
    uint8_t bytes[MaxCompatibility], size = 0;
    return p.schemaVersion == 2 && p.identityFieldCount && p.identitySource[0]
        && p.releaseReplaySafe && compatibility(p, bytes, size);
}
Modbus::Result readIdentity(Profiles::Profile const& p, Actuation::RegisterIo const& io, Identity& out) {
    out = {};
    if (!p.identityFieldCount || p.identityFieldCount > Profiles::MaxIdentityFields || !p.identitySource[0]) { return { Modbus::Error::InvalidData }; }
    for (size_t i = 0; i < p.identityFieldCount; ++i) {
        auto const& f = p.identityFields[i];
        if (!f.count || f.count > Profiles::MaxIdentityRegisters || uint32_t(f.address) + f.count > 65536) { return { Modbus::Error::InvalidData }; }
        auto read = f.space == Modbus::ReadFunction::InputRegisters ? io.readInputRegisters
            : f.space == Modbus::ReadFunction::HoldingRegisters ? io.readHoldingRegisters : nullptr;
        if (!read) { return { Modbus::Error::InvalidData }; }
        auto result = read(io.context, f.address, f.count, out.bytes + out.size);
        if (!result) { out = {}; return result; }
        size_t n = f.count * 2;
        bool zero = true, ones = true;
        for (size_t j = 0; j < n; ++j) { zero &= out.bytes[out.size + j] == 0; ones &= out.bytes[out.size + j] == 255; }
        if (zero || ones) { out = {}; return { Modbus::Error::InvalidData }; }
        out.size += n;
    }
    return {};
}

void Journal::publish(State state, Reason reason) {
    std::lock_guard<std::mutex> lock(_mutex);
    _snapshot.state = state; _snapshot.reason = reason; _snapshot.target = _target;
    if (state == State::Unresolved) { _snapshot.resolutionPending = false; }
    _snapshot.blocked = state != State::Idle && state != State::Armed && state != State::Recovered;
    _snapshot.resolutionAvailable = state == State::Unresolved && _recordValid
        && _record.possibleOwnership && !_snapshot.clearPending && _snapshot.recordId[0];
}
void Journal::load(Files& files, uint32_t (*clock)(), void (*random)(void*, size_t)) {
    _files = &files; _clock = clock; _random = random; _recordValid = false;
    uint8_t data[MaxRecord]; size_t size = 0;
    auto pending = files.read(_target, true, data, sizeof(data), size);
    auto loaded = files.read(_target, false, data, sizeof(data), size);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _snapshot = {}; _snapshot.target = _target;
        _snapshot.journalPresent = pending != LoadResult::Missing || loaded != LoadResult::Missing;
    }
    if (pending == LoadResult::Error || loaded == LoadResult::Error) { unresolved(Reason::JournalIo); return; }
    if (pending != LoadResult::Missing) { unresolved(Reason::JournalIncomplete); return; }
    if (loaded == LoadResult::Missing) { publish(State::Idle, Reason::None); return; }
    auto error = decode(data, size, _target, _record);
    if (error != Reason::None) { unresolved(error); return; }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _snapshot.journalPresent = _record.possibleOwnership;
        _snapshot.identityAvailable = _record.identity.size != 0;
        _snapshot.recordChecksum = checksum(data, size);
    }
    _recordValid = true;
    issueRecordId();
    publish(_record.possibleOwnership ? State::Pending : State::Idle, Reason::None);
}
Snapshot Journal::snapshot(uint32_t now) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto s = _snapshot;
    if (s.state == State::RetryWait) { s.retryInMs = now - _failureCompletedMs >= RetryMs ? 0 : RetryMs - (now - _failureCompletedMs); }
    return s;
}
bool Journal::due(uint32_t now) const {
    auto s = snapshot(now);
    return s.resolutionPending || s.state == State::Pending || (s.state == State::RetryWait && s.retryInMs == 0);
}
void Journal::issueRecordId() {
    if (!_random || !_record.possibleOwnership) { return; }
    uint8_t bytes[16]; _random(bytes, sizeof(bytes));
    static constexpr char Hex[] = "0123456789abcdef";
    std::lock_guard<std::mutex> lock(_mutex);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        _snapshot.recordId[i*2] = Hex[bytes[i] >> 4];
        _snapshot.recordId[i*2+1] = Hex[bytes[i] & 15];
    }
    _snapshot.recordId[32] = 0;
}
bool Journal::copyRecord(Record& record, Snapshot* snapshot) const {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_recordValid || !_record.possibleOwnership) { return false; }
    record = _record; if (snapshot) { *snapshot = _snapshot; } return true;
}
bool Journal::requestResolution(char const* recordId, bool independentlyReleased) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!independentlyReleased || !recordId || strlen(recordId) != 32
            || !_snapshot.resolutionAvailable || _snapshot.resolutionPending
            || strcmp(recordId, _snapshot.recordId)) { return false; }
    _snapshot.resolutionPending = true; _snapshot.resolutionAvailable = false;
    _snapshot.state = State::Resolving;
    return true;
}
bool Journal::resolve(uint32_t now) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_snapshot.resolutionPending || _snapshot.state != State::Resolving) { return false; }
        _snapshot.resolutionPending = false; _snapshot.operatorAcknowledged = true;
    }
    // Independent physical/vendor-interface release was explicitly attested for
    // this exact record. No Modbus operation is part of operator resolution.
    return clearReleased(now);
}
bool Journal::persist(Record const& record) {
    // Keep nested session/provider frames off the small worker stack. This
    // transient allocation is fixed-size, checked, and freed after one commit.
    auto data = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[MaxRecord]);
    size_t size = 0;
    if (!data || !_files || !encode(record, data.get(), size) || !_files->replace(_target, data.get(), size)) { return false; }
    std::lock_guard<std::mutex> lock(_mutex);
    _snapshot.recordChecksum = checksum(data.get(), size); return true;
}
bool Journal::arm(Endpoint const& endpoint, Profiles::Profile const& profile, uint32_t generation, Actuation::RegisterIo const& io) {
    auto s = snapshot();
    if (s.blocked) { return false; }
    if (s.state == State::Armed) {
        if (!endpointEqual(endpoint, _record.endpoint)) { unresolved(Reason::EndpointMismatch); return false; }
        if (!matching(_record, profile)) { unresolved(Reason::ProfileMismatch); return false; }
        return true;
    }
    Record record; record.target = _target; record.endpoint = endpoint; record.possibleOwnership = true;
    record.generation = generation;
    memcpy(record.profile, profile.id, sizeof(record.profile));
    if (!validEndpoint(endpoint) || !compatibility(profile, record.compatibility, record.compatibilitySize)) { unresolved(Reason::ReleaseInvalid); return false; }
    if (profile.identityFieldCount) {
        auto result = readIdentity(profile, io, record.identity);
        if (!result) { unresolved(Reason::IdentityReadFailed); return false; }
    }
    // No remote ownership write has occurred. Failure still blocks this boot:
    // a partial pending file must never be interpreted as an empty journal.
    if (!persist(record)) {
        { std::lock_guard<std::mutex> lock(_mutex); _snapshot.journalPresent = true; }
        unresolved(Reason::PersistFailed); return false;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _record = record; _recordValid = true;
        _snapshot.attempts = 0; _snapshot.lastResult = {};
        _snapshot.operatorAcknowledged = false; _snapshot.resolutionPending = false;
        _snapshot.journalPresent = true; _snapshot.identityAvailable = record.identity.size != 0;
        _snapshot.automaticSupported = automaticSupported(profile);
    }
    issueRecordId();
    publish(State::Armed, Reason::None); return true;
}
bool Journal::clearReleased(uint32_t now) {
    auto s = snapshot();
    if (s.state == State::Idle || s.state == State::Recovered) { return true; }
    // Only the live owner or identity-verified recovery may report a release.
    if (s.state != State::Armed && s.state != State::Running && s.state != State::Resolving && !s.clearPending) { return false; }
    Record cleared; cleared.target = _target;
    if (!persist(cleared)) {
        std::lock_guard<std::mutex> lock(_mutex);
        _snapshot.state = State::RetryWait; _snapshot.reason = Reason::ClearFailed;
        _snapshot.blocked = true; _snapshot.clearPending = true; _snapshot.resolutionAvailable = false;
        _failureCompletedMs = _clock ? _clock() : now;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _record = cleared; _recordValid = true;
        _snapshot.recordId[0] = 0; _snapshot.resolutionAvailable = false;
        _snapshot.journalPresent = false; _snapshot.clearPending = false;
    }
    publish(State::Recovered, Reason::None); return true;
}
bool Journal::retryClear(uint32_t now) {
    if (!snapshot().clearPending || !due(now)) { return false; }
    { std::lock_guard<std::mutex> lock(_mutex); ++_snapshot.attempts; }
    bool ok = clearReleased(now);
    if (!ok && snapshot().attempts >= MaxAttempts) { unresolved(Reason::ClearFailed); }
    return ok;
}
bool Journal::begin(Endpoint const& endpoint, Profiles::Profile const* profile, uint32_t now) {
    if (!due(now) || snapshot().clearPending) { return false; }
    if (!profile) { unresolved(Reason::ProfileMissing); return false; }
    if (!endpointEqual(endpoint, _record.endpoint)) { unresolved(Reason::EndpointMismatch); return false; }
    if (!matching(_record, *profile)) { unresolved(Reason::ProfileMismatch); return false; }
    if (!profile->identityFieldCount || !profile->identitySource[0]) { unresolved(Reason::IdentityUnsupported); return false; }
    if (!_record.identity.size) { unresolved(Reason::IdentityMissing); return false; }
    if (!profile->releaseReplaySafe) { unresolved(Reason::ReleaseNotReplaySafe); return false; }
    if (!automaticSupported(*profile)) { unresolved(Reason::ReleaseInvalid); return false; }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ++_snapshot.attempts; _snapshot.automaticSupported = true;
    }
    publish(State::CheckingIdentity, Reason::None); return true;
}
void Journal::attemptFinished(uint32_t now) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_snapshot.state == State::RetryWait) { _failureCompletedMs = now; }
}
void Journal::failed(Reason reason, uint32_t now, Actuation::CommandResult result) {
    std::lock_guard<std::mutex> lock(_mutex);
    _snapshot.lastResult = result; _snapshot.reason = reason;
    _snapshot.state = _snapshot.attempts >= MaxAttempts ? State::Unresolved : State::RetryWait;
    _snapshot.blocked = true; _failureCompletedMs = now;
    _snapshot.resolutionAvailable = _snapshot.state == State::Unresolved && _recordValid
        && _record.possibleOwnership && !_snapshot.clearPending && _snapshot.recordId[0];
}
void Journal::unresolved(Reason reason) { publish(State::Unresolved, reason); }
void Journal::recover(Profiles::Profile const& profile, Actuation::RegisterIo const& io, uint32_t (*now)()) {
    if (snapshot().state != State::CheckingIdentity) { return; }
    Identity current;
    auto read = readIdentity(profile, io, current);
    if (!read) {
        Actuation::CommandResult result; result.execution.transport = read;
        failed(Reason::IdentityReadFailed, now(), result); return;
    }
    if (current.size != _record.identity.size || memcmp(current.bytes, _record.identity.bytes, current.size)) { unresolved(Reason::IdentityMismatch); return; }
    if (!io.authorized || !io.authorized(io.context)) { unresolved(Reason::ConfigurationChanged); return; }
    publish(State::Running, Reason::None);
    // The same semantic release planner/executor as live retained cleanup.
    Actuation::ProfileSession session;
    auto result = session.release(io, profile, true);
    { std::lock_guard<std::mutex> lock(_mutex); _snapshot.lastResult = result; }
    if (!result) { failed(Reason::ReleaseFailed, now(), result); return; }
    clearReleased(now());
}
char const* stateName(State state) {
    switch (state) {
#define S(e, n) case State::e: return n
        S(NotLoaded, "not_loaded"); S(Idle, "idle"); S(Armed, "armed"); S(Pending, "recovery_pending");
        S(CheckingIdentity, "checking_identity"); S(Running, "recovery_running"); S(RetryWait, "retry_wait");
        S(Resolving, "operator_resolving"); S(Recovered, "recovered"); S(Unresolved, "unresolved");
#undef S
    }
    return "unknown";
}
char const* reasonName(Reason reason) {
    switch (reason) {
#define R(e, n) case Reason::e: return n
        R(None, "none"); R(NotLoaded, "not_loaded"); R(JournalIo, "journal_io"); R(JournalCorrupt, "journal_corrupt");
        R(JournalVersion, "journal_version"); R(JournalIncomplete, "journal_incomplete"); R(PersistFailed, "persist_failed");
        R(ClearFailed, "clear_failed"); R(ProfileMissing, "profile_missing"); R(ProfileMismatch, "profile_mismatch");
        R(EndpointMismatch, "endpoint_mismatch"); R(IdentityUnsupported, "identity_unsupported"); R(IdentityMissing, "identity_missing");
        R(IdentityMismatch, "identity_mismatch"); R(IdentityReadFailed, "identity_read_failed"); R(ReleaseNotReplaySafe, "release_not_replay_safe");
        R(ReleaseInvalid, "release_invalid"); R(TransportFailed, "transport_failed"); R(ReleaseFailed, "release_failed");
        R(ConfigurationChanged, "configuration_changed"); R(AttemptsExhausted, "attempts_exhausted");
#undef R
    }
    return "unknown";
}
} // namespace Hems::Recovery
