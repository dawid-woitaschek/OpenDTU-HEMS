// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Service.h>
#include <remote/Allocator.h>
#include <remote/Snapshots.h>
#include <remote/HemsSnapshot.h>
#include "remote_hems_fixture.h"
#include <ctime>
#include <RuntimeDiagnostics.h>
#include <SystemSnapshot.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <NtpSettings.h>
#include <cassert>
#include <iostream>
#include <fstream>
#include <string>
namespace {
Remote::Ring<Diagnostics::HeapSample> samples;
Remote::Ring<Diagnostics::HeapFailure> failures;
Remote::Ring<Diagnostics::HeapSample>::Entry sampleEntries[512];
Remote::Ring<Diagnostics::HeapFailure>::Entry failureEntries[64];
uint32_t checkpoint = 0, publishedRevision = 0, postDelayMs = 0;
uint32_t senderUtc = 1792180800;
unsigned attempts = 0, observerCheckpoints = 0;
uint32_t allocationFailureCount = 7;
bool accept = false, oversized = false, richWorst = false, snapshotFailure = false;
std::string lastBody;
void sample() { Diagnostics::HeapSample v; v.uptimeMs = millis(); samples.push(++checkpoint, v); }
}
extern "C" time_t time(time_t* out) noexcept { if (out) { *out = senderUtc; } return senderUtc; }
namespace Diagnostics {
uint32_t begin(Component, Operation, Phase, int8_t) { return 1; }
void finish(uint32_t) {}
void heapCheckpoint(HeapPoint point, HeapProvider, int8_t) { if (point == HeapPoint::Observer) ++observerCheckpoints; sample(); }
bool remoteStart() {
    if (samples.available()) return true;
    if (!testPsram || testMemoryFailure) return false;
    if (!samples.available()) { samples.attach(sampleEntries, 512); failures.attach(failureEntries, 64); sample(); }
    return true;
}
void remoteCollect() {}
void remoteExpire(uint32_t now) { samples.expire(now, Remote::RetentionMs, 512); failures.expire(now, Remote::RetentionMs, 64); }
RemoteCursor remoteCursor() { return {samples.oldest(), samples.newest(), samples.acknowledged(), samples.dropped(), failures.oldest(), failures.newest(), failures.dropped(), samples.count(), failures.count(), samples.available(), samples.expired(), failures.expired()}; }
bool remoteSample(uint32_t seq, HeapSample& out) { return samples.read(seq, out); }
bool remoteFailure(uint32_t seq, HeapFailure& out) { return failures.read(seq, out); }
void remoteAcknowledge(uint32_t seq, uint32_t failure) { samples.acknowledge(seq); failures.acknowledge(failure); }
void bootHeapSample(HeapSample& out) { out = {}; }
char const* remoteBootId() { return "9ee99ae59ee99ae59ee99ae59ee99ae5"; }
bool previousCapsule(Remote::Capsule& out) { out = {}; out.checkpoint = 19; Remote::seal(out); return true; }
void serialize(JsonObject out, bool history) { out["current_boot"]["valid"] = true; out["heap"]["checkpoint_count"] = checkpoint; out["heap"]["failed_alloc_count"] = allocationFailureCount; out["history_requested"] = history; }
}
// Hardware seams provide realistic cached inputs; Service builds the actual
// transmitted JSON and the device serializers below are production code.
void systemSnapshot(JsonObject out, bool) { out["uptime"] = millis() / 1000; out["firmware_version"] = "test"; out["resetreason_0"] = "Software reset CPU"; if (oversized) { for (int i = 0; i < 16; ++i) out["padding"].add(std::string(16384, 'x')); } }
void currentHeapSnapshot(JsonObject out) {
    out["internal"]["free_bytes"] = 128000U; out["internal"]["minimum_free_bytes"] = 100000U; out["internal"]["largest_free_block"] = 96000U;
    out["psram"]["total_bytes"] = 8 * 1024 * 1024;
    out["psram"]["free_bytes"] = 7000000U; out["psram"]["minimum_free_bytes"] = 6000000U; out["psram"]["largest_free_block"] = 6500000U;
}
namespace Remote {
uint32_t hemsPlanRevision() { return publishedRevision; }
bool hemsSnapshot(JsonObject out) { if (snapshotFailure) { return false; } out["read_only"] = true; out["global_mode"] = "Monitor"; out["optimizer"]["plan_revision"] = publishedRevision; if (richWorst) { worstHems(out, senderUtc); } return true; }
void deviceSnapshot(JsonObject storage, JsonObject wallbox) {
    Hems::Storage::Snapshot battery;
    battery.configured = battery.enabled = battery.profileLoaded = battery.reachable = battery.valid = battery.hasUpdate = true;
    battery.sample.socPct = {true, 63.5}; battery.sample.acPowerW = {true, 350};
    storageJson(battery, storage);
    Hems::Warp::Snapshot evse;
    evse.configured = evse.enabled = evse.reachable = evse.valid = evse.hasUpdate = true;
    evse.sample.chargingKnown = evse.sample.charging = true; evse.sample.chargingState = 3;
    evse.sample.allowedCurrentA = 16;
    wallboxJson(evse, wallbox);
}
bool post(Settings const&, char const* body, size_t size, Transmission const& flight, int& code, char const*& error) {
    ++attempts; lastBody.assign(body, size); code = 200; testNow += postDelayMs;
    JsonDocument doc; assert(!deserializeJson(doc, body, size));
    assert(doc["checkpoint_sequence"] == flight.checkpoint && doc["telemetry_sequence"] == flight.sequence);
    error = accept ? nullptr : "invalid_acknowledgement";
    return accept;
}
}
JsonDocument status(Remote::Service& service) { JsonDocument d; service.status(d.to<JsonObject>()); return d; }
JsonDocument config(bool enabled = true) { JsonDocument d; d["enabled"] = enabled; d["endpoint"] = "https://collector.example.org/api/v1/telemetry"; d["token"] = "hems_NEVER_IN_PAYLOAD"; d["interval_minutes"] = 60; return d; }
int main(int argc, char** argv) {
    assert(argc <= 2); // Optional output lets the collector's real parser consume this body.
    Remote::Service sender; sender.init(); assert(!sender.due()); sender.run(); assert(attempts == 0);
    testFiles["/remote-diagnostics.json"] = std::make_shared<std::string>("{\"version\":99}");
    Remote::Service unsupported; unsupported.init();
    JsonDocument unsupportedConfig; unsupported.configuration(unsupportedConfig.to<JsonObject>());
    assert(unsupportedConfig["enabled"] == false && unsupportedConfig["interval_minutes"] == 60);
    testFiles.clear();
    auto c = config(); assert(sender.configure(c.as<JsonVariantConst>()));
    assert(testFiles.count("/remote-diagnostics.json"));
    Remote::Service loaded; loaded.init(); JsonDocument publicConfig; loaded.configuration(publicConfig.to<JsonObject>());
    assert(publicConfig["enabled"] == true && publicConfig["token_set"] == true && publicConfig["token"].isNull());
    failCommit = true; c["interval_minutes"] = 120; assert(!sender.configure(c.as<JsonVariantConst>())); failCommit = false;
    publicConfig.clear(); sender.configuration(publicConfig.to<JsonObject>()); assert(publicConfig["interval_minutes"] == 60);
    testPsram = false; sender.run(); assert(attempts == 0 && status(sender)["last_error"] == "psram_required" && internalAttempts == 0);
    assert(!sender.due()); sender.requestSend(); assert(!sender.due()); // Manual requests respect backoff.
    testPsram = true; testNow += 60000; NetworkSettings.connected = false; sender.run(); assert(attempts == 0);
    NetworkSettings.connected = true; testNow += 120000; internalFree = 30000; sender.run(); assert(status(sender)["last_error"] == "insufficient_internal_heap");
    internalFree = 128 * 1024; testNow += 240000; sender.run();
    assert(attempts == 1 && status(sender)["retry_pending"] == true && status(sender)["telemetry_sequence"] == 1);
    auto original = lastBody; JsonDocument first; assert(!deserializeJson(first, original));
    if (argc == 2) { std::ofstream payload(argv[1]); payload << original; assert(payload.good()); }
    assert(first["diagnostics"]["allocation_failure_count"].is<uint32_t>() && first["diagnostics"]["allocation_failure_count"] == 7);
    assert(first["diagnostics"]["allocation_failures"].size() == 0); // Total failures are not pending-spool size.
    assert(original.find("hems_NEVER_IN_PAYLOAD") == std::string::npos && observerCheckpoints == 0);
    assert(first["diagnostics"]["previous_crash_capsule"]["checkpoint_sequence"] == 19);
    assert(first["diagnostics"]["runtime"]["history_requested"] == false);
    auto sentCheckpoint = first["checkpoint_sequence"].as<uint32_t>();
    assert(first["diagnostics"]["checkpoints"].as<JsonArrayConst>().size() <= Remote::BatchSamples);
    assert(first["diagnostics"]["checkpoints"][first["diagnostics"]["checkpoints"].size() - 1]["sequence"] == sentCheckpoint);
    assert(samples.count() > 0 && samples.acknowledged() == 0);
    allocationFailureCount = 9;
    sample(); sample(); testNow += 480000; accept = true; sender.run();
    assert(lastBody == original && status(sender)["telemetry_sequence"] == 2 && samples.acknowledged() == sentCheckpoint);
    assert(samples.count() > 0 && samples.oldest() > sentCheckpoint); // Newer checkpoints retained, including request diagnostics.
    assert(!sender.due()); testNow += 60000; assert(!sender.due()); // Normal heartbeat is hourly.
    sender.requestSend(); sender.run(); assert(status(sender)["telemetry_sequence"] == 3);
    JsonDocument second; assert(!deserializeJson(second, lastBody)); assert(second["diagnostics"]["previous_crash_capsule"].isNull());
    assert(second["diagnostics"]["allocation_failure_count"] == 9); // Only a newly built body reflects the new counter.
    testNow += 60000; auto before = attempts; oversized = true; sender.requestSend(); sender.run();

    assert(attempts == before && status(sender)["last_error"] == "payload_overflow" && samples.count() > 0);
    oversized = false; testMemoryFailure = true; testNow += 60000; sender.run(); assert(attempts == before && status(sender)["last_error"] == "psram_json_exhausted");
    testMemoryFailure = false; testNow += 120000; sender.run(); assert(attempts == before + 1);
    assert(internalAttempts == 0);
    c = config(false); c["token"] = ""; assert(sender.configure(c.as<JsonVariantConst>()));
    assert(!sender.due() && !sender.requestSend());
    publicConfig.clear(); sender.configuration(publicConfig.to<JsonObject>()); assert(publicConfig["token_set"] == false);
    Remote::Service copyFailure; copyFailure.init(); c = config(); assert(copyFailure.configure(c.as<JsonVariantConst>()));
    auto beforeCopyFailure = attempts; snapshotFailure = true; copyFailure.run();
    assert(status(copyFailure)["last_error"] == "psram_hems_snapshot_exhausted" && status(copyFailure)["payload_bytes"] == 0);
    assert(status(copyFailure)["telemetry_sequence"] == 1 && attempts == beforeCopyFailure);
    snapshotFailure = false; testNow += 60000; copyFailure.run(); assert(status(copyFailure)["telemetry_sequence"] == 2);
    // UTC slot triggers, immutable retries and latest-state coalescing in the real sender.
    c = config(); c["quarter_hour"] = true; c["interval_minutes"] = 15;
    Remote::Service events; events.init(); assert(events.configure(c.as<JsonVariantConst>()));
    accept = true; events.run(); auto periodicBody = lastBody;
    JsonDocument event; assert(!deserializeJson(event, lastBody)); assert(event["hems"]["trigger"][0] == "periodic");
    assert(event["hems"]["captured_at_utc"] == senderUtc && event["hems"]["read_only"] == true);
    testNow += 900000; senderUtc += 900; events.run();
    assert(!deserializeJson(event, lastBody));
    bool periodic = false, quarter = false;
    auto triggerArray = event["hems"]["trigger"].as<JsonArrayConst>();
    for (auto flag : triggerArray) { periodic |= flag == "periodic"; quarter |= flag == "quarter_hour"; }
    assert(periodic && quarter); auto sent = attempts; events.run(); assert(attempts == sent);
    testNow += 60000; accept = false; assert(events.requestSend()); events.run(); auto pendingBody = lastBody;
    for (unsigned revision = 1; revision <= 100; ++revision) { publishedRevision = revision; events.due(); }
    assert(status(events)["last_optimizer_plan_revision_observed"] == 100 && status(events)["coalesced_hems_trigger_count"].as<unsigned>() >= 99);
    assert(status(events)["telemetry_sequence"] == 3 && status(events)["payload_bytes"] == pendingBody.size());
    testNow += 60000; events.run(); assert(lastBody == pendingBody);
    testNow += 120000; accept = true; events.run(); assert(lastBody == pendingBody);
    testNow += 60000; events.run(); assert(!deserializeJson(event, lastBody));
    assert(event["hems"]["optimizer"]["plan_revision"] == 100 && event["hems"]["trigger"][0] == "plan_revision");
    assert(periodicBody != lastBody);
    // Worst bounded arrays plus diagnostic records fit the unchanged PSRAM/byte caps.
    testNow += 60000; richWorst = true;
    for (unsigned i = 0; i < 160; ++i) { sample(); }
    for (unsigned i = 1; i <= 64; ++i) { Diagnostics::HeapFailure failure; failure.uptimeMs = testNow; failures.push(i, failure); }
    events.requestSend(); events.run();
    assert(!deserializeJson(event, lastBody));
    assert(event["hems"]["tariff"]["serialized_interval_count"] == 96 && event["hems"]["optimizer"]["schedule"].size() == 96);
    assert(event["hems"]["forecast"]["truncated"] == true && event["hems"]["optimizer"]["schedule_truncated"] == true);
    assert(lastBody.size() <= Remote::MaxPayload && status(events)["last_error"].isNull());
    assert(lastBody.find("PRIVATE_PROVIDER_SECRET") == std::string::npos && lastBody.find("collector.example.org") == std::string::npos);
    assert(lastBody.find("52123456") == std::string::npos && lastBody.find("hems_NEVER_IN_PAYLOAD") == std::string::npos);
    assert(event["diagnostics"]["checkpoint_batch_truncated"] == true);
    richWorst = false;
    // A lost ACK must not pin the sender to an old snapshot for more than an hour.
    Remote::Service outage; outage.init(); accept = false; outage.run();
    auto expiredBody = lastBody; JsonDocument oldBody; assert(!deserializeJson(oldBody, expiredBody));
    auto oldSequence = oldBody["telemetry_sequence"].as<unsigned>(); auto acknowledged = samples.acknowledged();
    testNow += Remote::RetentionMs - 1; senderUtc += 3599; publishedRevision = 101;
    assert(status(outage)["pending_transmission_age_seconds"] == 3599);
    ++testNow; ++senderUtc; outage.run();
    assert(status(outage)["expired_transmission_count"] == 1 && samples.acknowledged() == acknowledged);
    assert(!deserializeJson(event, lastBody)); assert(lastBody != expiredBody);
    assert(event["telemetry_sequence"] == oldSequence + 1 && event["hems"]["optimizer"]["plan_revision"] == 101);
    // During a prolonged outage there is still only one body and bounded sample rings.
    auto outageAttempts = attempts;
    for (unsigned minute = 0; minute < 240; ++minute) {
        testNow += 60000; senderUtc += 60; ++publishedRevision; sample(); outage.run();
        assert(status(outage)["payload_bytes"].as<size_t>() <= Remote::MaxPayload);
        assert(samples.count() <= 512);
    }
    assert(attempts - outageAttempts < 25 && status(outage)["expired_transmission_count"].as<unsigned>() >= 3);
    assert(samples.acknowledged() == acknowledged && internalAttempts == 0);
    // Disabled senders also release an expired body, without ACKing anything.
    c["enabled"] = false; assert(outage.configure(c.as<JsonVariantConst>()));
    testNow += Remote::RetentionMs; senderUtc += 3600; outage.run();
    assert(status(outage)["payload_bytes"] == 0 && status(outage)["retry_pending"] == false && samples.acknowledged() == acknowledged);
    Remote::Service lateAck; lateAck.init(); c["enabled"] = true; assert(lateAck.configure(c.as<JsonVariantConst>()));
    accept = false; lateAck.run(); auto ackBeforeLate = samples.acknowledged();
    testNow += Remote::RetentionMs - 10000; senderUtc += 3590; postDelayMs = 20000; accept = true;
    lateAck.run(); postDelayMs = 0;
    assert(status(lateAck)["expired_transmission_count"] == 1 && status(lateAck)["payload_bytes"] == 0 && samples.acknowledged() == ackBeforeLate);
    auto lateAttempts = attempts; lateAck.run(); assert(attempts == lateAttempts); // No TLS burst after a late ACK is discarded.
    testNow += 120000; senderUtc += 120; lateAck.run();
    assert(status(lateAck)["telemetry_sequence"] == 3 && status(lateAck)["last_error"].isNull());
    std::cout << "remote sender persistence/scheduling/identity/payload/memory passed\n";
}
