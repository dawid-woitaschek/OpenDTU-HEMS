// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Service.h>
#include <remote/Allocator.h>
#include <remote/Snapshots.h>
#include <remote/HemsSnapshot.h>
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
#include <algorithm>
namespace Remote {
namespace {
constexpr char Path[] = "/remote-diagnostics.json", Temp[] = "/remote-diagnostics.tmp";
constexpr size_t MaxConfig = 2048;
bool elapsed(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

}
Service Sender;
void Service::init() {
    _schedule.periodicMs = _earliestMs = millis();
    _schedule.slot = time(nullptr) >= 1577836800 ? time(nullptr) / 900 : 0;
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
    if (!wasEnabled && _settings.enabled) {
        _schedule.add(Periodic, _flight.pending); _schedule.periodicMs = millis();
        if (!_attempted || millis() - _lastAttemptMs > RetentionMs) { _earliestMs = millis(); }
    }
    if (!_settings.enabled) { _schedule.pending = 0; }
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
    out["busy"] = _busy; out["send_requested"] = (_schedule.pending & Manual) != 0;
    out["last_attempt_uptime_ms"] = nullptr; out["last_success_uptime_ms"] = nullptr;
    if (_attempted) { out["last_attempt_uptime_ms"] = _lastAttemptMs; }
    if (_succeeded) { out["last_success_uptime_ms"] = _lastSuccessMs; }
    out["last_http_result"] = _httpResult;
    out["last_error"] = _error;
    out["runtime_reason"] = prerequisite(_settings);
    out["next_upload_in_seconds"] = nullptr;
    if (_settings.enabled) {
        auto now = millis(); auto next = _flight.pending || _schedule.pending ? _earliestMs : _schedule.periodicMs;
        uint32_t seconds = elapsed(now, next) ? 0U : (next - now + 999) / 1000;
        auto utc = time(nullptr);
        if (!_flight.pending && !_schedule.pending && _settings.quarterHour && utc >= 1577836800) {
            seconds = std::min(seconds, 900U - static_cast<uint32_t>(utc) % 900);
            if (!elapsed(now, _earliestMs)) { seconds = std::max<uint32_t>(seconds, (_earliestMs - now + 999) / 1000); }
        }
        out["next_upload_in_seconds"] = seconds;
    }
    out["telemetry_sequence"] = _flight.sequence; out["retry_pending"] = _flight.pending;
    out["last_acknowledged_checkpoint_sequence"] = cursor.acknowledged;
    out["oldest_available_sequence"] = cursor.oldest; out["newest_sequence"] = cursor.newest;
    out["pending_samples"] = cursor.pending; out["dropped_samples"] = cursor.dropped;
    out["pending_allocation_failures"] = cursor.pendingFailures; out["dropped_allocation_failures"] = cursor.failureDropped;
    out["spool_available"] = cursor.available; out["spool_capacity"] = SampleCapacity;
    out["quarter_hour"] = _settings.quarterHour;
    out["pending_transmission_age_seconds"] = nullptr;
    if (_flight.pending) { out["pending_transmission_age_seconds"] = (millis() - _flight.stagedMs) / 1000; }
    out["expired_transmission_count"] = _expiredTransmissions; out["dropped_telemetry_count"] = _droppedTelemetry;
    out["coalesced_hems_trigger_count"] = _schedule.coalesced;
    out["last_hems_capture_utc"] = nullptr;
    if (_lastHemsUtc) { out["last_hems_capture_utc"] = _lastHemsUtc; }
    out["last_hems_capture_uptime_ms"] = _lastHemsMs;
    triggersJson(_lastHemsTriggers, out["last_hems_trigger"].to<JsonArray>());
    triggersJson(_schedule.pending, out["pending_hems_trigger"].to<JsonArray>());
    out["last_optimizer_plan_revision_observed"] = _schedule.revision;
    out["expired_samples"] = cursor.expired; out["expired_allocation_failures"] = cursor.failuresExpired;
    out["payload_bytes"] = _bodySize; out["payload_max_bytes"] = MaxPayload;
}
bool Service::requestSend() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_settings.enabled) { return false; }
    _schedule.add(Manual, _flight.pending || _busy); return true;
}
void Service::expireLocked(uint32_t now) {
    if (!_flight.expired(now)) { return; }
    free(_body); _body = nullptr; _bodySize = 0;
    _flight.abandon(); // New identity before any different body; no remoteAcknowledge.
    ++_expiredTransmissions; ++_droppedTelemetry;
    _schedule.add(DiagnosticEvent, false); _error = "transmission_expired";
}
bool Service::due() {
    Diagnostics::remoteCollect();
    Diagnostics::remoteExpire(millis());
    auto cursor = Diagnostics::remoteCursor();
    auto revision = hemsPlanRevision(); // Scalar published revision only, no planner work.
    std::lock_guard<std::mutex> lock(_mutex);
    auto now = millis();
    if (_settings.enabled) {
        _schedule.observe(now, time(nullptr), revision, _settings.quarterHour, _flight.pending || _busy);
        if (cursor.failureNewest > _seenFailure) {
            _seenFailure = cursor.failureNewest; _schedule.add(DiagnosticEvent, _flight.pending || _busy);
        }
        if (cursor.pending >= SampleCapacity * 3 / 4 && !(_schedule.pending & DiagnosticEvent)) {
            _schedule.add(DiagnosticEvent, _flight.pending || _busy);
        }
    }
    if (_busy) { return false; }
    if (_flight.expired(now)) { return true; } // Expire even when disabled; free only on the worker.
    return _settings.enabled && elapsed(now, _earliestMs) && (_flight.pending || _schedule.pending);
}
bool Service::buildPayload(uint8_t triggers) {
    auto capturedUtc = static_cast<uint32_t>(time(nullptr)); auto capturedMs = millis();
    Allocator allocator(384 * 1024); JsonDocument doc(&allocator);
    doc["schema_version"] = 1; doc["boot_id"] = Diagnostics::remoteBootId();
    doc["telemetry_sequence"] = _flight.sequence;
    systemSnapshot(doc["system"].to<JsonObject>(), false); // No observer checkpoint.
    doc["system"]["reset_reason"] = doc["system"]["resetreason_0"];
    currentHeapSnapshot(doc["heap"].to<JsonObject>());
    deviceSnapshot(doc["storage"].to<JsonObject>(), doc["wallbox"].to<JsonObject>());
    auto hems = doc["hems"].to<JsonObject>();
    hems["captured_at_utc"] = capturedUtc; hems["captured_at_uptime_ms"] = capturedMs;
    triggersJson(triggers, hems["trigger"].to<JsonArray>());
    if (!hemsSnapshot(hems)) { _error = "psram_hems_snapshot_exhausted"; return false; }
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
    auto failures = diagnostics["allocation_failures"].to<JsonArray>();
    uint32_t lastFailure = 0;
    if (cursor.pendingFailures) {
        for (uint32_t seq = cursor.failureOldest; seq <= cursor.failureNewest; ++seq) {
            Diagnostics::HeapFailure value;
            if (!Diagnostics::remoteFailure(seq, value)) { _error = "spool_overwritten_during_snapshot"; return false; }
            auto row = failures.add<JsonObject>(); row["sequence"] = seq; Diagnostics::heapFailureJson(value, row); lastFailure = seq;
        }
    }
    auto rows = diagnostics["checkpoints"].to<JsonArray>();
    uint32_t highest = 1; // The boot baseline is included even if no rolling checkpoint fits.
    auto baseBytes = measureJson(doc);
    // Each fixed checkpoint is <1024 serialized bytes. Reserve capsule/metadata
    // space and prefer current HEMS data over a stale diagnostic backlog.
    auto batchLimit = std::min(BatchSamples, baseBytes + 8192 < MaxPayload ? (MaxPayload - baseBytes - 8192) / 1024 : size_t(0));
    if (cursor.pending) {
        for (uint32_t seq = cursor.oldest; seq <= cursor.newest && rows.size() < batchLimit; ++seq) {
            Diagnostics::HeapSample value;
            if (!Diagnostics::remoteSample(seq, value)) { _error = "spool_overwritten_during_snapshot"; return false; }
            auto row = rows.add<JsonObject>(); row["sequence"] = seq; Diagnostics::heapSampleJson(value, row); highest = seq;
        }
    }
    diagnostics["available_checkpoint_count"] = cursor.pending;
    diagnostics["serialized_checkpoint_count"] = rows.size();
    diagnostics["checkpoint_batch_truncated"] = rows.size() < cursor.pending;
    diagnostics["expired_samples"] = cursor.expired; diagnostics["expired_allocation_failures"] = cursor.failuresExpired;
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
    _body = body; _bodySize = length; _flight.stage(highest, lastFailure, millis());
    _schedule.captured(triggers, capturedMs, _settings.intervalMinutes);
    _lastHemsUtc = capturedUtc; _lastHemsMs = capturedMs; _lastHemsTriggers = triggers;
    return true;
}
void Service::run() {
    if (!due()) { return; }
    Settings config;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_busy) { return; }
        expireLocked(millis());
        if (!_settings.enabled || !elapsed(millis(), _earliestMs)) { return; }
        _busy = true; _attempted = true; _lastAttemptMs = millis(); config = _settings;
    }
    Diagnostics::Scope activity(Diagnostics::Component::RemoteDiagnostics, Diagnostics::Operation::Upload);
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::BeforeRequest);
    bool ok = false;
    char const* error = prerequisite(config); int httpResult = 0;
    try {
        if (!Diagnostics::remoteStart() && !error) { error = "psram_spool_exhausted"; }
        if (!error) {
            // Only worker mutates the flight; status reads are synchronized.
            { std::lock_guard<std::mutex> lock(_mutex); if (!_flight.pending && !buildPayload(_schedule.pending)) { error = _error; } }
            if (!error) { ok = post(config, _body, _bodySize, _flight, httpResult, error); }
        }
    } catch (std::bad_alloc const&) { error = "insufficient_memory"; }
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Released);
    Diagnostics::remoteCollect();
    auto now = millis();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _httpResult = httpResult; _error = error;
        if (_flight.expired(now)) {
            expireLocked(now); ok = false; _error = "transmission_expired";
            auto retry = retrySeconds(_failures); if (_failures < 5) { ++_failures; }
            _earliestMs = now + retry * 1000;
        } else if (ok) {
            Diagnostics::remoteAcknowledge(_flight.checkpoint, _flight.failure);
            _flight.complete(); _previousSent = true; free(_body); _body = nullptr; _bodySize = 0;
            _failures = 0; _succeeded = true; _lastSuccessMs = now;
            _earliestMs = now + 60000;
            if (Diagnostics::remoteCursor().pending > BatchSamples) { _schedule.add(DiagnosticEvent, false); }
        } else {
            auto retry = retrySeconds(_failures); if (_failures < 5) { ++_failures; }
            _earliestMs = now + retry * 1000;
        }
        _busy = false;
    }
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::WorkerIdle);
}
} // namespace Remote
