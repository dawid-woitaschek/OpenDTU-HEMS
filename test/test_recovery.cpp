// SPDX-License-Identifier: GPL-2.0-or-later
#include "recovery_files.h"
#include <hems/recovery/PathGuard.h>
#include <hems/actuation/Readiness.h>
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <thread>
#include <atomic>
using namespace Hems;
using namespace Hems::Recovery;
static uint32_t clockMs = 0;
static uint32_t clockNow() { return clockMs; }
static constexpr auto Target = ControlTarget::Storage;
static Profiles::Profile fixture() {
    // Entirely synthetic: these are not WARP/Marstek hardware addresses.
    Profiles::Profile p;
    p.schemaVersion = 2; strcpy(p.id, "test.recovery");
    p.capabilities = Profiles::capabilityBit(Profiles::Capability::Storage);
    p.acquireWriteCount = 1; p.releaseFirstWrite = 1; p.releaseWriteCount = 1;
    p.controlWriteCount = 3; p.controlOperationCount = 1;
    p.controlWrites[0].address = 10; p.controlWrites[0].constant = 1;
    p.controlWrites[1].address = 11; p.controlWrites[1].constant = 0;
    p.controlWrites[2].address = 12; p.controlWrites[2].constant = 20;
    auto& operation = p.controlOperations[0];
    strcpy(operation.key, "stop"); operation.firstWrite = 2; operation.writeCount = 1;
    p.identityFieldCount = 1; p.identityFields[0] = { Modbus::ReadFunction::InputRegisters, 90, 2 };
    strcpy(p.identitySource, "Synthetic immutable UUID fixture; no physical device semantics");
    p.releaseReplaySafe = true;
    return p;
}
struct Harness {
    MemoryJournalFiles files;
    Journal journal { Target };
    Profiles::Profile profile = fixture();
    Endpoint endpoint;
    std::map<uint16_t, uint16_t> registers { {90, 0x1234}, {91, 0xabcd} };
    bool authorized = true, failWrite = false, failRead = false;
    unsigned writes = 0, reads = 0;
    std::function<void()> beforeWrite, afterIdentity;
    Harness() { strcpy(endpoint.host, "synthetic.invalid"); endpoint.port = 502; endpoint.unit = 1; journal.load(files); }
    static bool gate(void* c) { return static_cast<Harness*>(c)->authorized; }
    static Modbus::Result write(void* c, uint16_t address, uint16_t count, uint16_t const* values) {
        auto& h = *static_cast<Harness*>(c);
        if (h.beforeWrite) { h.beforeWrite(); }
        ++h.writes;
        for (unsigned i = 0; i < count; ++i) { h.registers[address + i] = values[i]; }
        if (h.failWrite) { clockMs += RetryMs + 1000; return { Modbus::Error::Timeout }; }
        return {};
    }
    static Modbus::Result read(void* c, uint16_t address, uint16_t count, uint8_t* data) {
        auto& h = *static_cast<Harness*>(c); ++h.reads;
        if (h.failRead) { clockMs += RetryMs + 1000; return { Modbus::Error::Timeout }; }
        for (unsigned i = 0; i < count; ++i) { auto v = h.registers[address + i]; data[i*2] = v >> 8; data[i*2+1] = v; }
        if (address == 90 && h.afterIdentity) { h.afterIdentity(); }
        return {};
    }
    static bool arm(void* c) { auto& h = *static_cast<Harness*>(c); return h.journal.arm(h.endpoint, h.profile, 42, h.io()); }
    static bool clear(void* c) { return static_cast<Harness*>(c)->journal.clearReleased(clockMs); }
    Actuation::RegisterIo io(bool lifecycle = true) {
        return { this, write, read, gate, lifecycle ? arm : nullptr, lifecycle ? clear : nullptr, read };
    }
    void own() { Actuation::ProfileSession session; assert(session.execute(io(), profile, "stop")); }
    void boot(Journal& rebooted) { rebooted.load(files); assert(rebooted.blocked()); }
};
static void parserTests() {
    JsonDocument d;
    assert(!deserializeJson(d, Profiles::exampleJson()));
    d["schema"] = Profiles::SchemaV2;
    d["identity"]["kind"] = "device_instance"; d["identity"]["source"] = "synthetic provenance";
    auto f = d["identity"]["fields"].to<JsonArray>().add<JsonObject>();
    f["space"] = "holding"; f["address"] = 90; f["count"] = 2; f["encoding"] = "register_bytes";
    d["recovery"]["release_replay_safe"] = true;
    auto parse = [&](bool expected) {
        std::string json; serializeJson(d, json);
        Profiles::Profile p; char const* error = nullptr;
        assert(Profiles::parse(json.data(), json.size(), p, error) == expected);
        return p;
    };
    auto p = parse(true); assert(p.identityFieldCount == 1 && p.releaseReplaySafe);
    JsonDocument roundtrip; Profiles::serialize(p, roundtrip.to<JsonObject>());
    std::string serialized; serializeJson(roundtrip, serialized);
    char const* error = nullptr; Profiles::Profile copy;
    assert(Profiles::parse(serialized.data(), serialized.size(), copy, error));
    assert(copy.identityFields[0].address == 90);
    f["count"] = 0; parse(false); f["count"] = 9; parse(false); f["count"] = 2;
    f["address"] = 65535; parse(false); f["address"] = 90;
    f["space"] = "discrete"; parse(false); f["space"] = "holding";
    f["encoding"] = "float32"; parse(false); f["encoding"] = "register_bytes";
    d["identity"]["kind"] = "firmware_version"; parse(false); d["identity"]["kind"] = "device_instance";
    d["recovery"]["release_replay_safe"] = "true"; parse(false);
    d.remove("recovery"); assert(!parse(true).releaseReplaySafe);
    d["schema"] = Profiles::SchemaV1; parse(false);
}
int main() {
    parserTests();
    for (auto path : { "hems-recovery-evse.bin", "/hems-recovery-storage.pending", "//./hems-recovery-evse.bin",
            "/x/../hems-recovery-storage.bin", "/littlefs/hems-recovery-evse.bin", "/hems-recovery-future-version" }) {
        assert(protectedPath(path));
    }
    assert(!protectedPath("/config.json") && !protectedPath("/hems-profiles/0.json"));
    // 1, 3: Intent and captured identity are durable before the very first wire
    // write; uncertain response retains ownership and the same durable record.
    {
        Harness h; Actuation::ProfileSession session;
        h.beforeWrite = [&] {
            Journal rebooted(Target); h.boot(rebooted);
            assert(rebooted.snapshot().journalPresent && rebooted.snapshot().identityAvailable);
        };
        h.failWrite = true;
        auto result = session.execute(h.io(), h.profile, "stop");
        assert(!result && session.ownsControl() && h.writes == 1);
        assert(h.journal.snapshot().state == State::Armed);
    }
    // 2: Preflight and authorization rejection before a first write never arm.
    {
        Harness h; Actuation::ProfileSession session; h.authorized = false;
        assert(!session.execute(h.io(), h.profile, "stop"));
        assert(h.files.commits == 0 && !session.ownsControl());
        h.authorized = true;
        assert(!session.execute(h.io(), h.profile, "unknown"));
        h.profile.releaseWriteCount = 0;
        assert(!session.execute(h.io(), h.profile, "stop"));
        assert(h.files.commits == 0 && h.writes == 0);
    }
    // Acquire-less profiles own only after a possible operation write.
    {
        Harness h; h.profile.acquireWriteCount = 0; Actuation::ProfileSession session;
        h.failWrite = true;
        assert(!session.execute(h.io(), h.profile, "stop"));
        assert(session.ownsControl() && h.files.commits == 1);
    }
    // Persistence failure prevents wire writes, including interrupted temp files.
    for (bool partial : { false, true }) {
        Harness h; h.files.fail = true; h.files.partial = partial;
        Actuation::ProfileSession session;
        auto r = session.execute(h.io(), h.profile, "stop");
        assert(r.error == Actuation::CommandError::PersistenceFailed && !h.writes && h.journal.blocked());
        if (partial) { Journal rebooted(Target); h.boot(rebooted); assert(rebooted.snapshot().reason == Reason::JournalIncomplete); }
    }
    // 4, 5: remote release/readback and local ownership precede clear. A failed
    // durable clear blocks, retries only flash, and survives reboot as pending.
    for (bool clearFails : { false, true }) {
        Harness h; Actuation::ProfileSession session;
        assert(session.execute(h.io(), h.profile, "stop"));
        h.files.fail = clearFails;
        auto r = session.release(h.io(), h.profile);
        assert(bool(r) == !clearFails && !session.ownsControl());
        Journal rebooted(Target); rebooted.load(h.files);
        assert(rebooted.blocked() == clearFails);
        if (clearFails) {
            assert(h.journal.snapshot().clearPending && h.journal.blocked());
            auto before = h.writes; auto completed = clockMs;
            assert(!h.journal.due(completed + RetryMs - 1));
            assert(h.journal.due(completed + RetryMs));
            h.files.fail = false;
            assert(h.journal.retryClear(completed + RetryMs));
            assert(h.writes == before && !h.journal.blocked());
        }
    }
    // 6..9: Valid, corrupt, truncated, unknown-version, pending, and storage I/O
    // failure are all distinguishable; no load mutates persisted evidence.
    {
        Harness h; h.own(); auto original = h.files.committed[1];
        for (unsigned kind = 0; kind < 4; ++kind) {
            h.files.committed[1] = original;
            auto& bytes = h.files.committed[1];
            if (kind == 1) { bytes[100] ^= 1; }
            if (kind == 2) { bytes.resize(bytes.size()/2); }
            if (kind == 3) { bytes[4] = 99; }
            Journal j(Target); h.boot(j);
            assert(j.snapshot().state == (kind ? State::Unresolved : State::Pending));
            assert(j.snapshot().reason == (kind == 1 || kind == 2 ? Reason::JournalCorrupt : kind == 3 ? Reason::JournalVersion : Reason::None));
        }
        h.files.committed[1] = original;
        h.files.hasPending[1] = true; h.files.pending[1].clear();
        Journal incomplete(Target); h.boot(incomplete); assert(incomplete.snapshot().reason == Reason::JournalIncomplete);
        h.files.ioError = true;
        Journal unavailable(Target); h.boot(unavailable); assert(unavailable.snapshot().reason == Reason::JournalIo);
    }
    // 10..13, 17, 21: Reject statically without even identity I/O, and never
    // retarget recovery to a changed endpoint/profile or missing identity.
    for (unsigned kind = 0; kind < 7; ++kind) {
        Harness h;
        if (kind == 4) { h.profile.identityFieldCount = 0; }
        if (kind == 5) { h.profile.releaseReplaySafe = false; }
        h.own(); Journal j(Target); h.boot(j);
        auto p = h.profile; auto endpoint = h.endpoint;
        if (kind == 1) { strcpy(p.id, "test.replacement"); }
        if (kind == 2) { p.controlWrites[1].constant = 42; }
        if (kind == 3) { strcpy(endpoint.host, "replacement.invalid"); }
        if (kind == 6) { endpoint.unit = 2; }
        unsigned writes = h.writes, reads = h.reads;
        assert(!j.begin(endpoint, kind == 0 ? nullptr : &p, clockMs));
        assert(j.snapshot().state == State::Unresolved && h.writes == writes && h.reads == reads);
    }
    // Explicit missing identity in an otherwise compatible record.
    {
        Harness h; h.own(); Record r; auto& bytes = h.files.committed[1];
        assert(decode(bytes.data(), bytes.size(), Target, r) == Reason::None);
        r.identity = {}; uint8_t out[MaxRecord]; size_t n;
        assert(encode(r, out, n)); h.files.replace(Target, out, n);
        Journal j(Target); h.boot(j); assert(!j.begin(h.endpoint, &h.profile, clockMs));
        assert(j.snapshot().reason == Reason::IdentityMissing);
    }
    // 14..16, 18: Exact stable identity required; failed reads/release back off
    // from completion, with a finite attempt budget, and matching recovery uses
    // exactly the bounded semantic release path.
    for (unsigned kind = 0; kind < 5; ++kind) {
        Harness h; h.own(); Journal j(Target); h.boot(j);
        unsigned before = h.writes;
        if (kind == 1) { h.registers[90] ^= 1; }
        if (kind == 2) { h.failRead = true; }
        if (kind == 3) { h.failWrite = true; }
        if (kind == 4) { h.afterIdentity = [&] { h.authorized = false; }; }
        assert(j.begin(h.endpoint, &h.profile, clockMs));
        j.recover(h.profile, h.io(false), clockNow);
        auto s = j.snapshot(clockMs);
        assert(h.writes == before + ((kind == 0 || kind == 3) ? 1 : 0));
        assert(s.blocked == (kind != 0));
        if (kind == 0) { assert(s.state == State::Recovered && !s.journalPresent); }
        if (kind == 1) { assert(s.reason == Reason::IdentityMismatch); }
        if (kind == 4) { assert(s.reason == Reason::ConfigurationChanged); }
        if (kind == 2 || kind == 3) {
            auto completed = clockMs;
            assert(!j.due(completed + RetryMs - 1) && j.due(completed + RetryMs));
            assert(!j.begin(h.endpoint, &h.profile, completed + RetryMs - 1));
            for (unsigned attempt = 1; attempt < MaxAttempts; ++attempt) {
                clockMs += RetryMs;
                assert(j.begin(h.endpoint, &h.profile, clockMs));
                j.recover(h.profile, h.io(false), clockNow);
            }
            assert(j.snapshot().state == State::Unresolved && j.snapshot().attempts == MaxAttempts);
        }
    }
    // Identity concatenation preserves field boundaries by exact layout pinning;
    // all-zero and all-ones sentinel data are not accepted as instance identity.
    {
        Harness h; h.profile.identityFieldCount = 2;
        h.profile.identityFields[1] = { Modbus::ReadFunction::HoldingRegisters, 100, 1 };
        h.registers[100] = 0x6789; Identity id;
        assert(readIdentity(h.profile, h.io(), id) && id.size == 6 && id.bytes[4] == 0x67);
        h.registers[100] = 0; assert(!readIdentity(h.profile, h.io(), id));
        h.registers[100] = 65535; assert(!readIdentity(h.profile, h.io(), id));
    }
    // 19: The common readiness gate includes unresolved durable recovery.
    {
        Actuation::ReadinessInput in { true, true, true, true, true, true, true, false, false, true };
        auto r = Actuation::evaluateReadiness(in);
        assert(!r.allowed() && !r.readyForActive() && r.has(Actuation::ReadinessBlocker::RecoveryPending));
    }
    // Authenticated HTTP queues this protocol; worker alone clears. The nonce
    // binds the acknowledgement to one boot and one exact loaded record. Merely
    // reading status or presenting a stale nonce cannot clear a blocker.
    {
        auto random = +[](void* out, size_t n) {
            static uint8_t seed = 1; auto* bytes = static_cast<uint8_t*>(out);
            for (size_t i = 0; i < n; ++i) { bytes[i] = seed++; }
        };
        Harness h; h.profile.identityFieldCount = 0; h.own();
        auto originalFiles = h.files;
        Journal j(Target); j.load(h.files, clockNow, random);
        assert(!j.begin(h.endpoint, &h.profile, clockMs));
        auto snapshot = j.snapshot(); assert(snapshot.resolutionAvailable && snapshot.recordId[0]);
        assert(!j.requestResolution(snapshot.recordId, false));
        assert(!j.requestResolution("00000000000000000000000000000000", true));
        assert(j.requestResolution(snapshot.recordId, true));
        assert(!j.requestResolution(snapshot.recordId, true));
        auto writes = h.writes, commits = h.files.commits;
        assert(j.blocked() && h.files.commits == commits); // enqueue has no flash/network work
        h.files.fail = true; assert(!j.resolve(clockMs));
        assert(j.blocked() && j.snapshot().clearPending && j.snapshot().operatorAcknowledged);
        h.files.fail = false; assert(j.retryClear(clockMs + RetryMs));
        assert(!j.blocked() && h.writes == writes);
        assert(!j.requestResolution(snapshot.recordId, true));
        Journal nextBoot(Target); nextBoot.load(originalFiles, clockNow, random);
        assert(!nextBoot.begin(h.endpoint, &h.profile, clockMs));
        assert(!nextBoot.requestResolution(snapshot.recordId, true));
        Record viewed; Snapshot viewedSnapshot;
        assert(nextBoot.copyRecord(viewed, &viewedSnapshot));
        assert(!strcmp(viewed.endpoint.host, h.endpoint.host) && strcmp(viewedSnapshot.recordId, snapshot.recordId));
        originalFiles.committed[1][100] ^= 1;
        Journal corrupt(Target); corrupt.load(originalFiles, clockNow, random);
        assert(!corrupt.snapshot().resolutionAvailable && !corrupt.requestResolution(viewedSnapshot.recordId, true));
    }
    // 22: Concurrent diagnostics never see partial state/counters or mutable
    // record storage. Only the provider worker mutates lifecycle state.
    {
        Harness h; std::atomic<bool> done { false };
        std::thread reader([&] {
            while (!done) { auto s = h.journal.snapshot(clockMs); assert(s.target == Target); }
        });
        for (unsigned i = 0; i < 50; ++i) { h.own(); assert(h.journal.clearReleased()); }
        done = true; reader.join();
    }
    std::cout << "Crash-consistent recovery, identity, policy, retry and snapshot tests passed\n";
}
