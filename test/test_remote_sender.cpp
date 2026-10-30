// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Service.h>
#include <remote/Allocator.h>
#include <remote/Snapshots.h>
#include <RuntimeDiagnostics.h>
#include <SystemSnapshot.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <NtpSettings.h>
#include <cassert>
#include <iostream>
#include <string>
namespace {
Remote::Ring<Diagnostics::HeapSample> samples;
Remote::Ring<Diagnostics::HeapFailure> failures;
Remote::Ring<Diagnostics::HeapSample>::Entry sampleEntries[512];
Remote::Ring<Diagnostics::HeapFailure>::Entry failureEntries[16];
uint32_t checkpoint = 0;
unsigned attempts = 0, observerCheckpoints = 0;
bool accept = false, oversized = false;
std::string lastBody;
void sample() { Diagnostics::HeapSample v; v.uptimeMs = millis(); samples.push(++checkpoint, v); }
}
namespace Diagnostics {
uint32_t begin(Component, Operation, Phase, int8_t) { return 1; }
void finish(uint32_t) {}
void heapCheckpoint(HeapPoint point, HeapProvider, int8_t) { if (point == HeapPoint::Observer) ++observerCheckpoints; sample(); }
bool remoteStart() {
    if (samples.available()) return true;
    if (!testPsram || testMemoryFailure) return false;
    if (!samples.available()) { samples.attach(sampleEntries, 512); failures.attach(failureEntries, 16); sample(); }
    return true;
}
void remoteCollect() {}
RemoteCursor remoteCursor() { return {samples.oldest(), samples.newest(), samples.acknowledged(), samples.dropped(), failures.oldest(), failures.newest(), failures.dropped(), samples.count(), failures.count(), samples.available()}; }
bool remoteSample(uint32_t seq, HeapSample& out) { return samples.read(seq, out); }
bool remoteFailure(uint32_t seq, HeapFailure& out) { return failures.read(seq, out); }
void remoteAcknowledge(uint32_t seq, uint32_t failure) { samples.acknowledge(seq); failures.acknowledge(failure); }
void bootHeapSample(HeapSample& out) { out = {}; }
char const* remoteBootId() { return "9ee99ae59ee99ae59ee99ae59ee99ae5"; }
bool previousCapsule(Remote::Capsule& out) { out = {}; out.checkpoint = 19; Remote::seal(out); return true; }
void serialize(JsonObject out, bool history) { out["current_boot"]["valid"] = true; out["heap"]["checkpoint_count"] = checkpoint; out["history_requested"] = history; }
}
void systemSnapshot(JsonObject out, bool) { out["uptime"] = millis() / 1000; out["firmware_version"] = "test"; if (oversized) { for (int i = 0; i < 16; ++i) out["padding"].add(std::string(16384, 'x')); } }
void currentHeapSnapshot(JsonObject out) { out["psram"]["total_bytes"] = 8 * 1024 * 1024; }
namespace Remote {
void deviceSnapshot(JsonObject storage, JsonObject wallbox) { storage["provider"] = "test"; wallbox["provider"] = "warp"; }
bool post(Settings const&, char const* body, size_t size, Transmission const& flight, int& code, char const*& error) {
    ++attempts; lastBody.assign(body, size); code = 200;
    JsonDocument doc; assert(!deserializeJson(doc, body, size));
    assert(doc["checkpoint_sequence"] == flight.checkpoint && doc["telemetry_sequence"] == flight.sequence);
    error = accept ? nullptr : "invalid_acknowledgement";
    return accept;
}
}
JsonDocument status(Remote::Service& service) { JsonDocument d; service.status(d.to<JsonObject>()); return d; }
JsonDocument config(bool enabled = true) { JsonDocument d; d["enabled"] = enabled; d["endpoint"] = "https://collector.example.org/api/v1/telemetry"; d["token"] = "hems_NEVER_IN_PAYLOAD"; d["interval_minutes"] = 60; return d; }
int main() {
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
    assert(original.find("hems_NEVER_IN_PAYLOAD") == std::string::npos && observerCheckpoints == 0);
    assert(first["diagnostics"]["previous_crash_capsule"]["checkpoint_sequence"] == 19);
    assert(first["diagnostics"]["runtime"]["history_requested"] == false);
    auto sentCheckpoint = first["checkpoint_sequence"].as<uint32_t>();
    assert(first["diagnostics"]["checkpoints"].as<JsonArrayConst>().size() <= Remote::BatchSamples);
    assert(first["diagnostics"]["checkpoints"][first["diagnostics"]["checkpoints"].size() - 1]["sequence"] == sentCheckpoint);
    assert(samples.count() > 0 && samples.acknowledged() == 0);
    sample(); sample(); testNow += 480000; accept = true; sender.run();
    assert(lastBody == original && status(sender)["telemetry_sequence"] == 2 && samples.acknowledged() == sentCheckpoint);
    assert(samples.count() > 0 && samples.oldest() > sentCheckpoint); // Newer checkpoints retained, including request diagnostics.
    assert(!sender.due()); testNow += 60000; assert(!sender.due()); // Normal heartbeat is hourly.
    sender.requestSend(); sender.run(); assert(status(sender)["telemetry_sequence"] == 3);
    JsonDocument second; assert(!deserializeJson(second, lastBody)); assert(second["diagnostics"]["previous_crash_capsule"].isNull());
    testNow += 60000; auto before = attempts; oversized = true; sender.requestSend(); sender.run();

    assert(attempts == before && status(sender)["last_error"] == "payload_overflow" && samples.count() > 0);
    oversized = false; testMemoryFailure = true; testNow += 60000; sender.run(); assert(attempts == before && status(sender)["last_error"] == "psram_json_exhausted");
    testMemoryFailure = false; testNow += 120000; sender.run(); assert(attempts == before + 1);
    assert(internalAttempts == 0);
    c = config(false); c["token"] = ""; assert(sender.configure(c.as<JsonVariantConst>()));
    assert(!sender.due() && !sender.requestSend());
    publicConfig.clear(); sender.configuration(publicConfig.to<JsonObject>()); assert(publicConfig["token_set"] == false);
    std::cout << "remote sender persistence/scheduling/identity/payload/memory passed\n";
}
