// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Service.h>
#include <remote/Allocator.h>
#include <remote/Snapshots.h>
#include <RuntimeDiagnostics.h>
#include <SystemSnapshot.h>
#include <diagnostics/HeapSerialize.h>
#include <hems/energy/Service.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <NtpSettings.h>
#include <Arduino.h>
#include <ctime>
#include <new>
namespace Remote {
namespace {
constexpr char Path[] = "/remote-diagnostics.json", Temp[] = "/remote-diagnostics.tmp";
constexpr size_t MaxConfig = 2048;
bool elapsed(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}
Service Sender;
void Service::init() {
    _nextMs = _earliestMs = millis();
    load();
    if (_settings.enabled && !Diagnostics::remoteStart()) { _error = "psram_spool_exhausted"; }
    Hems::Energy::EnergyData.diagnosticsBackground(
        [](void* p) { return static_cast<Service*>(p)->due(); },
        [](void* p) { static_cast<Service*>(p)->run(); }, this);
}
bool Service::load() {
    auto file = LittleFS.open(Path, "r");
    if (!file || !file.size() || file.size() > MaxConfig) { return false; }
    JsonDocument doc;
    Settings candidate;
    if (deserializeJson(doc, file) || !doc["version"].is<unsigned>() || doc["version"].as<unsigned>() != 1
        || !parseSettings(doc["settings"], candidate, true)) { return false; }
    _settings = candidate; return true;
}
void Service::configuration(JsonObject out) const {
    std::lock_guard<std::mutex> lock(_mutex); settingsJson(_settings, out);
}
bool Service::configure(JsonVariantConst input) {
    Diagnostics::Scope activity(Diagnostics::Component::Configuration, Diagnostics::Operation::Write);
    std::lock_guard<std::mutex> lock(_mutex);
    Settings candidate = _settings;
    if (!parseSettings(input, candidate)) { return false; }
    JsonDocument doc; doc["version"] = 1; settingsJson(candidate, doc["settings"].to<JsonObject>(), true);
    auto length = measureJson(doc);
    if (doc.overflowed() || length > MaxConfig) { return false; }
    LittleFS.remove(Temp); auto file = LittleFS.open(Temp, "w");
    bool ok = file && serializeJson(doc, file) == length;
    if (file) { file.flush(); file.close(); }
    auto verify = LittleFS.open(Temp, "r"); JsonDocument checked; Settings confirmed;
    ok = ok && verify && verify.size() == length && !deserializeJson(checked, verify)
        && checked["version"].is<unsigned>() && checked["version"].as<unsigned>() == 1
        && parseSettings(checked["settings"], confirmed, true);
    verify.close();
    if (!ok || !LittleFS.rename(Temp, Path)) { LittleFS.remove(Temp); return false; }
    bool wasEnabled = _settings.enabled;
    _settings = confirmed;
    if (!wasEnabled && _settings.enabled) { _manual = true; _nextMs = _earliestMs = millis(); }
    if (!_settings.enabled) { _manual = false; }
    return true;
}
char const* Service::prerequisite(Settings const& config) const {
    if (!config.enabled) { return "disabled"; }
    if (!validEndpoint(config.endpoint) || !config.token[0]) { return "invalid_config"; }
    if (!heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) { return "psram_required"; }
    if (!NetworkSettings.isConnected()) { return "network_unavailable"; }
    if (!NtpSettings.isTimeSynced() || time(nullptr) < 1704067200) { return "clock_invalid"; }
    constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(caps) < 80 * 1024 || heap_caps_get_largest_free_block(caps) < 40 * 1024) { return "insufficient_internal_heap"; }
    return nullptr;
}
void Service::status(JsonObject out) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto cursor = Diagnostics::remoteCursor();
    out["enabled"] = _settings.enabled; out["token_set"] = _settings.token[0] != 0;
    out["busy"] = _busy; out["send_requested"] = _manual;
    out["last_attempt_uptime_ms"] = nullptr; out["last_success_uptime_ms"] = nullptr;
    if (_attempted) { out["last_attempt_uptime_ms"] = _lastAttemptMs; }
    if (_succeeded) { out["last_success_uptime_ms"] = _lastSuccessMs; }
    out["last_http_result"] = _httpResult;
    out["last_error"] = _error;
    out["runtime_reason"] = prerequisite(_settings);
    out["next_upload_in_seconds"] = nullptr;
    if (_settings.enabled) {
        auto now = millis(); auto next = _manual ? _earliestMs : _nextMs;
        out["next_upload_in_seconds"] = elapsed(now, next) ? 0U : (next - now + 999) / 1000;
    }
    out["telemetry_sequence"] = _flight.sequence; out["retry_pending"] = _flight.pending;
    out["last_acknowledged_checkpoint_sequence"] = cursor.acknowledged;
    out["oldest_available_sequence"] = cursor.oldest; out["newest_sequence"] = cursor.newest;
    out["pending_samples"] = cursor.pending; out["dropped_samples"] = cursor.dropped;
    out["pending_allocation_failures"] = cursor.pendingFailures; out["dropped_allocation_failures"] = cursor.failureDropped;
    out["spool_available"] = cursor.available; out["spool_capacity"] = SampleCapacity;
    out["payload_bytes"] = _bodySize; out["payload_max_bytes"] = MaxPayload;
}
bool Service::requestSend() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_settings.enabled || _busy) { return false; }
    _manual = true; return true;
}
bool Service::due() {
    Diagnostics::remoteCollect(); // Copies failures in safe scheduler context; no JSON/network/allocation.
    auto cursor = Diagnostics::remoteCursor();
    std::lock_guard<std::mutex> lock(_mutex);
    auto now = millis();
    if (!_settings.enabled || _busy || !elapsed(now, _earliestMs)) { return false; }
    return _manual || elapsed(now, _nextMs) || cursor.pending >= SampleCapacity * 3 / 4 || cursor.failureNewest > _seenFailure;
}
bool Service::buildPayload() {
    Allocator allocator(384 * 1024); JsonDocument doc(&allocator);
    doc["schema_version"] = 1; doc["boot_id"] = Diagnostics::remoteBootId();
    doc["telemetry_sequence"] = _flight.sequence;
    systemSnapshot(doc["system"].to<JsonObject>(), false); // No observer checkpoint.
    doc["system"]["reset_reason"] = doc["system"]["resetreason_0"];
    currentHeapSnapshot(doc["heap"].to<JsonObject>());
    deviceSnapshot(doc["storage"].to<JsonObject>(), doc["wallbox"].to<JsonObject>());
    Diagnostics::remoteCollect();
    auto cursor = Diagnostics::remoteCursor();
    auto diagnostics = doc["diagnostics"].to<JsonObject>();
    Diagnostics::serialize(diagnostics["runtime"].to<JsonObject>(), false);
    // Alias the authoritative current-boot counter, not retained/spool counts.
    diagnostics["allocation_failure_count"] = diagnostics["runtime"]["heap"]["failed_alloc_count"];
    Diagnostics::HeapSample boot;
    Diagnostics::bootHeapSample(boot);
    auto baseline = diagnostics["boot_baseline"].to<JsonObject>(); baseline["sequence"] = 1;
    Diagnostics::heapSampleJson(boot, baseline);
    diagnostics["oldest_available_sequence"] = cursor.oldest; diagnostics["newest_sequence"] = cursor.newest;
    diagnostics["dropped_samples"] = cursor.dropped; diagnostics["dropped_allocation_failures"] = cursor.failureDropped;
    auto rows = diagnostics["checkpoints"].to<JsonArray>();
    uint32_t highest = cursor.acknowledged;
    if (cursor.pending) {
        for (uint32_t seq = cursor.oldest; seq <= cursor.newest && rows.size() < BatchSamples; ++seq) {
            Diagnostics::HeapSample value;
            if (!Diagnostics::remoteSample(seq, value)) { _error = "spool_overwritten_during_snapshot"; return false; }
            auto row = rows.add<JsonObject>(); row["sequence"] = seq; Diagnostics::heapSampleJson(value, row); highest = seq;
        }
    }
    auto failures = diagnostics["allocation_failures"].to<JsonArray>();
    uint32_t lastFailure = 0;
    if (cursor.pendingFailures) {
        for (uint32_t seq = cursor.failureOldest; seq <= cursor.failureNewest; ++seq) {
            Diagnostics::HeapFailure value;
            if (!Diagnostics::remoteFailure(seq, value)) { _error = "spool_overwritten_during_snapshot"; return false; }
            auto row = failures.add<JsonObject>(); row["sequence"] = seq; Diagnostics::heapFailureJson(value, row); lastFailure = seq;
        }
    }
    if (!_previousSent) {
        Capsule capsule;
        if (Diagnostics::previousCapsule(capsule)) {
            auto out = diagnostics["previous_crash_capsule"].to<JsonObject>();
            out["boot_id"] = static_cast<char const*>(capsule.boot); out["checkpoint_sequence"] = capsule.checkpoint;
            out["description"] = "last_completed_checkpoint"; out["intentional_restart"] = capsule.intentionalRestart != 0;
            out["failed_allocation_count"] = capsule.failureCount;
            Diagnostics::heapSampleJson(capsule.sample, out["checkpoint"].to<JsonObject>());
            if (capsule.failureCount) { Diagnostics::heapFailureJson(capsule.failure, out["latest_failure"].to<JsonObject>()); }
        }
    }
    doc["checkpoint_sequence"] = highest;
    auto length = measureJson(doc);
    if (doc.overflowed()) { _error = "psram_json_exhausted"; return false; }
    if (!length || length > MaxPayload) { _error = "payload_overflow"; return false; }
    auto body = static_cast<char*>(externalAllocate(length + 1));
    if (!body) { _error = "psram_payload_exhausted"; return false; }
    if (serializeJson(doc, body, length + 1) != length) { free(body); _error = "payload_overflow"; return false; }
    _body = body; _bodySize = length; _flight.stage(highest, lastFailure);
    return true;
}
void Service::run() {
    if (!due()) { return; }
    Settings config;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_busy || !_settings.enabled) { return; }
        _busy = true; _manual = false; _attempted = true; _lastAttemptMs = millis(); config = _settings;
    }
    Diagnostics::Scope activity(Diagnostics::Component::RemoteDiagnostics, Diagnostics::Operation::Upload);
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::BeforeRequest);
    bool ok = false;
    char const* error = prerequisite(config); int httpResult = 0;
    try {
        if (!Diagnostics::remoteStart() && !error) { error = "psram_spool_exhausted"; }
        if (!error) {
            // Only worker mutates the flight; status reads are synchronized.
            { std::lock_guard<std::mutex> lock(_mutex); if (!_flight.pending && !buildPayload()) { error = _error; } }
            if (!error) { ok = post(config, _body, _bodySize, _flight, httpResult, error); }
        }
    } catch (std::bad_alloc const&) { error = "insufficient_memory"; }
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Released);
    Diagnostics::remoteCollect();
    auto now = millis();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _httpResult = httpResult; _error = error;
        if (ok) {
            Diagnostics::remoteAcknowledge(_flight.checkpoint, _flight.failure);
            _seenFailure = _flight.failure > _seenFailure ? _flight.failure : _seenFailure;
            _flight.complete(); _previousSent = true; free(_body); _body = nullptr; _bodySize = 0;
            _failures = 0; _succeeded = true; _lastSuccessMs = now;
            _earliestMs = now + 60000;
            _nextMs = now + (Diagnostics::remoteCursor().pending > BatchSamples ? 60000 : uint32_t(_settings.intervalMinutes) * 60000);
        } else {
            auto retry = retrySeconds(_failures); if (_failures < 5) { ++_failures; }
            _earliestMs = _nextMs = now + retry * 1000;
        }
        _busy = false;
    }
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::WorkerIdle);
}
} // namespace Remote
