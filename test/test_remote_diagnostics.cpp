// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Core.h>
#include <remote/Allocator.h>
#include <remote/Snapshots.h>
#include <hems/storage/Poller.h>
#include <hems/evse/Poller.h>
#include <hems/warp/Poller.h>
#include <cassert>
#include <string>
#include <iostream>
using namespace Remote;
int main() {
    Settings settings;
    assert(!settings.enabled && settings.intervalMinutes == 60 && !settings.token[0]);
    for (auto url : {"https://collector.example.org/api/v1/telemetry", "https://a.b:8443/path-v1_x/~x"}) assert(validEndpoint(url));
    for (auto url : {"", "http://example.org/a", "HTTPS://example.org/a", "https://user:pass@example.org/a", "https://user@example.org/a", "https://example.org", "https://localhost/a", "https://127.0.0.1/a", "https://[::1]/a", "https://-example.org/a", "https://example..org/a", "https://example.org:0/a", "https://example.org:65536/a", "https://example.org/a?token=x", "https://example.org/a#x", "https://example.org/\\a", "https://example.org/%0d", "https://example.org/\r\nX:x", "https://example.org /a", "https://example.org@evil.org/a"}) assert(!validEndpoint(url));
    JsonDocument doc;
    settingsJson(settings, doc.to<JsonObject>()); assert(doc["token_set"] == false && doc["token"].isNull());
    doc["endpoint"] = "https://collector.example.org/api/v1/telemetry"; doc["enabled"] = true;
    assert(!parseSettings(doc.as<JsonVariantConst>(), settings));
    doc["token"] = "hems_secret-SHOULD-NEVER-LEAK";
    assert(parseSettings(doc.as<JsonVariantConst>(), settings));
    std::string token = settings.token;
    doc.clear(); settingsJson(settings, doc.to<JsonObject>()); std::string redacted; serializeJson(doc, redacted);
    assert(redacted.find(token) == std::string::npos && doc["token_set"] == true);
    doc["interval_minutes"] = 120; assert(parseSettings(doc.as<JsonVariantConst>(), settings)); assert(token == settings.token);
    doc["token"] = "bad\r\nAuthorization:x"; assert(!parseSettings(doc.as<JsonVariantConst>(), settings));
    doc["token"] = ""; assert(!parseSettings(doc.as<JsonVariantConst>(), settings));
    doc["enabled"] = false; assert(parseSettings(doc.as<JsonVariantConst>(), settings)); assert(!settings.token[0]);
    doc.clear(); settingsJson(settings, doc.to<JsonObject>(), true);
    Settings rebooted; assert(parseSettings(doc.as<JsonVariantConst>(), rebooted, true)); assert(rebooted.intervalMinutes == 120 && !rebooted.enabled);
    doc["interval_minutes"] = 0; assert(!parseSettings(doc.as<JsonVariantConst>(), settings));
    doc["interval_minutes"] = 1441; assert(!parseSettings(doc.as<JsonVariantConst>(), settings));
    Ring<Diagnostics::HeapSample> ring; Diagnostics::HeapSample sample, read;
    ring.push(1, sample); assert(!ring.available() && ring.count() == 0);
    Ring<Diagnostics::HeapSample>::Entry entries[3]; ring.attach(entries, 3);
    for (uint32_t i = 1; i <= 5; ++i) { sample.uptimeMs = i * 10; ring.push(i, sample); }
    assert(ring.count() == 3 && ring.oldest() == 3 && ring.newest() == 5 && ring.dropped() == 2);
    assert(!ring.read(2, read)); for (unsigned i = 3; i <= 5; ++i) { assert(ring.read(i, read) && read.uptimeMs == i * 10); }
    Transmission flight; flight.stage(4, 0);
    assert(!flight.accepts(doc.as<JsonVariantConst>()));
    doc.clear(); doc["accepted"] = true;
    assert(!flight.accepts(doc.as<JsonVariantConst>()));
    for (auto json : {"{\"accepted\":false,\"accepted_checkpoint_sequence\":4}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":\"4\"}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":5}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":-1}", "{\"accepted\":true,\"accepted_checkpoint_sequence\":4.5}"}) {
        assert(!deserializeJson(doc, json)); assert(!flight.accepts(doc.as<JsonVariantConst>()));
    }
    assert(flight.sequence == 1 && ring.count() == 3); // All failures retain identity and pending samples.
    assert(!deserializeJson(doc, "{\"accepted\":true,\"duplicate\":true,\"accepted_checkpoint_sequence\":4}"));
    assert(flight.accepts(doc.as<JsonVariantConst>())); ring.acknowledge(flight.checkpoint); flight.complete();
    assert(flight.sequence == 2 && !flight.pending && ring.count() == 1 && ring.oldest() == 5 && ring.acknowledged() == 4);
    ring.acknowledge(99); assert(ring.count() == 1); // Never advance beyond transmitted/newest data.
    Capsule capsule {}; assert(!valid(capsule)); capsule.sample.freeInternal = 100; capsule.checkpoint = 42;
    seal(capsule); assert(valid(capsule)); capsule.sample.freeInternal ^= 1; assert(!valid(capsule));
    seal(capsule); capsule.version = 2; assert(!valid(capsule));
    uint8_t random[16] {}; char id[33], second[33]; bootId(random, id); random[0] = 42; bootId(random, second);
    assert(strlen(id) == 32 && strcmp(id, second));
    assert(retrySeconds(0) == 60 && retrySeconds(20) == 1920);
    Remote::Allocator bounded(64); assert(!bounded.allocate(MaxPayload)); auto small = bounded.allocate(32); assert(small); assert(!bounded.reallocate(small, 65)); bounded.deallocate(small);
    Hems::Storage::Snapshot storage; storage.hasUpdate = true; storage.valid = true; storage.sample.socPct = {true, 63.5};
    storage.sample.chargeEnergyTotalKWh = {true, 55.2}; storage.sample.faultCode = {true, 7};
    uint8_t bytes[] {0x12, 0x34, 0xab, 0xcd};
    storage.sample.raw.record(Hems::Modbus::ReadFunction::HoldingRegisters, 32104, 2, bytes);
    doc.clear(); storageJson(storage, doc.to<JsonObject>());
    assert(doc["last_observations"]["soc_pct"] == 63.5 && doc["last_observations"]["fault_code"] == 7);
    assert(doc["raw_modbus"]["registers"][1]["address"] == 32105 && doc["raw_modbus"]["registers"][1]["value"] == 0xabcd);
    assert(doc["last_observations"]["temperature_c"].isNull());
    std::string payload; serializeJson(doc, payload); assert(payload.find(token) == std::string::npos);
    Hems::Warp::Snapshot wallbox; wallbox.hasUpdate = true; wallbox.sample.chargingKnown = true; wallbox.sample.charging = true;
    wallbox.sample.allowedCurrentA = 16; wallbox.sample.meterAvailable = true; wallbox.sample.powerW = 11040; wallbox.sample.chargingState = 3;
    doc.clear(); wallboxJson(wallbox, doc.to<JsonObject>());
    assert(doc["last_observations"]["charging"] == true && doc["last_observations"]["allowed_current_a"] == 16 && doc["last_observations"]["power_w"] == 11040);
    for (unsigned i = 0; i < 60; ++i) wallbox.sample.raw.record(Hems::Modbus::ReadFunction::InputRegisters, i, 1, bytes);
    assert(wallbox.sample.raw.count == 48 && wallbox.sample.raw.dropped == 12);
    std::cout << "remote diagnostics core/snapshots passed\n";
}
