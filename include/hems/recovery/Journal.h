// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/Control.h>
#include <hems/actuation/ProfileSession.h>
#include <mutex>

namespace Hems::Recovery {
constexpr size_t MaxHost = 253;
constexpr size_t MaxCompatibility = 128;
constexpr size_t MaxRecord = 544;
constexpr uint32_t RetryMs = 30000;
constexpr uint32_t MaxAttempts = 5;

struct Endpoint {
    char host[MaxHost + 1] {};
    uint16_t port = 0;
    uint8_t unit = 0;
};
struct Identity {
    uint8_t bytes[Profiles::MaxIdentityBytes] {};
    uint8_t size = 0;
};
struct Record {
    ControlTarget target = ControlTarget::Evse;
    Endpoint endpoint;
    char profile[Profiles::MaxId + 1] {};
    uint8_t compatibility[MaxCompatibility] {};
    uint8_t compatibilitySize = 0;
    Identity identity;
    uint32_t generation = 0; // Diagnostic only; never authorizes a reboot write.
    bool possibleOwnership = false;
};

enum class LoadResult : uint8_t { Missing, Present, Error };
// replace must flush/close, verify and atomically rename pending over committed.
// Failed/incomplete replacements must leave evidence; pending is never ignored.
class Files {
public:
    virtual ~Files() = default;
    virtual LoadResult read(ControlTarget target, bool pending, uint8_t* data,
            size_t capacity, size_t& size) = 0;
    virtual bool replace(ControlTarget target, uint8_t const* data, size_t size) = 0;
};
Files& runtimeFiles();

enum class State : uint8_t { NotLoaded, Idle, Armed, Pending, CheckingIdentity, Running, RetryWait, Recovered, Unresolved, Resolving };
enum class Reason : uint8_t {
    None, NotLoaded, JournalIo, JournalCorrupt, JournalVersion, JournalIncomplete,
    PersistFailed, ClearFailed, ProfileMissing, ProfileMismatch, EndpointMismatch,
    IdentityUnsupported, IdentityMissing, IdentityMismatch, IdentityReadFailed,
    ReleaseNotReplaySafe, ReleaseInvalid, TransportFailed, ReleaseFailed,
    ConfigurationChanged, AttemptsExhausted,
};
struct Snapshot {
    State state = State::NotLoaded;
    Reason reason = Reason::NotLoaded;
    ControlTarget target = ControlTarget::Evse;
    bool journalPresent = false;
    bool automaticSupported = false;
    bool identityAvailable = false;
    bool identitySupported = false;
    bool blocked = true;
    bool clearPending = false;
    bool resolutionPending = false;
    bool operatorAcknowledged = false;
    bool resolutionAvailable = false;
    char recordId[33] {};
    uint32_t attempts = 0;
    uint32_t retryInMs = 0;
    uint32_t recordChecksum = 0; // Diagnostics only, never an identity/authorization.
    Actuation::CommandResult lastResult {};
};
char const* stateName(State state);
char const* reasonName(Reason reason);
uint32_t checksum(uint8_t const* bytes, size_t size);
bool encode(Record const& record, uint8_t* data, size_t& size);
Reason decode(uint8_t const* data, size_t size, ControlTarget target, Record& record);
bool compatibility(Profiles::Profile const& profile, uint8_t* data, uint8_t& size);
bool automaticSupported(Profiles::Profile const& profile);
Modbus::Result readIdentity(Profiles::Profile const& profile, Actuation::RegisterIo const& io, Identity& identity);

// One worker mutates a journal. HTTP/scheduler readers only take bounded locked
// snapshots. Filesystem and Modbus operations never run while holding this mutex.
class Journal {
public:
    explicit Journal(ControlTarget target) : _target(target) { _snapshot.target = target; }
    void load(Files& files, uint32_t (*clock)() = nullptr,
            void (*random)(void*, size_t) = nullptr);
    Snapshot snapshot(uint32_t now = 0) const;
    bool copyRecord(Record& record, Snapshot* snapshot = nullptr) const;
    bool requestResolution(char const* recordId, bool independentlyReleased);
    bool resolve(uint32_t now);
    bool blocked() const { return snapshot().blocked; }
    bool due(uint32_t now) const;
    bool arm(Endpoint const& endpoint, Profiles::Profile const& profile, uint32_t generation,
            Actuation::RegisterIo const& io);
    bool clearReleased(uint32_t now = 0);
    bool retryClear(uint32_t now);
    // No network work occurs until every static prerequisite passes.
    bool begin(Endpoint const& current, Profiles::Profile const* profile, uint32_t now);
    void recover(Profiles::Profile const& profile, Actuation::RegisterIo const& io, uint32_t (*now)());
    void attemptFinished(uint32_t now);
    void failed(Reason reason, uint32_t now, Actuation::CommandResult result = {});
    void unresolved(Reason reason);
private:
    void publish(State state, Reason reason);
    bool persist(Record const& record);
    void issueRecordId();
    ControlTarget _target;
    Files* _files = nullptr;
    uint32_t (*_clock)() = nullptr;
    Record _record;
    bool _recordValid = false;
    void (*_random)(void*, size_t) = nullptr;
    mutable std::mutex _mutex;
    Snapshot _snapshot;
    uint32_t _failureCompletedMs = 0;
};
} // namespace Hems::Recovery
