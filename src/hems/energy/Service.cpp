// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Service.h>
#include <RuntimeDiagnostics.h>
#include <hems/Memory.h>
#include <hems/profiles/Profile.h>
#include <HttpGetter.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <NtpSettings.h>
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

namespace Hems::Energy {
namespace {
constexpr char Path[] = "/hems-energy.json", Temp[] = "/hems-energy.tmp";
char const* name(Domain domain) { return domain == Domain::Tariff ? "tariff" : "forecast"; }
Diagnostics::HeapProvider heapProvider(Source source)
{
    return source == Source::EnergyCharts ? Diagnostics::HeapProvider::EnergyCharts
        : source == Source::ForecastSolar ? Diagnostics::HeapProvider::ForecastSolar : Diagnostics::HeapProvider::Normalized;
}
uint32_t utcNow() { auto now = time(nullptr); return now > 0 && static_cast<uint64_t>(now) <= UINT32_MAX ? now : 0; }
bool nativeCloud(Source source) { return source == Source::EnergyCharts || source == Source::ForecastSolar; }
bool clockReady(Settings const& settings)
{
    return utcNow() >= MinUtc && (!nativeCloud(settings.source) || NtpSettings.isTimeSynced());
}
void intervalJson(Domain domain, Interval const& interval, JsonObject output)
{
    output["start"] = interval.start; output["end"] = interval.end;
    if (domain == Domain::Tariff) {
        output["price_micros_per_kwh"] = interval.value;
        output["import_price_micros_per_kwh"] = nullptr;
        if (interval.hasImportPrice) { output["import_price_micros_per_kwh"] = interval.importPrice; }
    } else {
        output["power_w"] = interval.value;
        output["energy_wh"] = static_cast<double>(interval.value) * (interval.end - interval.start) / 3600;
    }
}
void nativeJson(NativeInfo const& info, JsonObject out)
{
    out["http_status"] = info.httpStatus;
    out["http_error"] = info.httpError[0] ? info.httpError : nullptr;
    out["tls_error"] = info.tlsError;
    out["tls_error_text"] = info.tlsErrorText[0] ? info.tlsErrorText : nullptr;
    out["dns"] = info.dns < 0 ? "not_attempted" : info.dns ? "resolved" : "failed";
    out["duration_ms"] = info.durationMs; out["payload_bytes"] = info.payloadBytes; out["json_peak_bytes"] = info.jsonPeak;
    out["rate_period_seconds"] = info.ratePeriodSeconds; out["retry_after_seconds"] = info.retrySeconds;
    serializeMemory(info.before, out["before"].to<JsonObject>());
    serializeMemory(info.connected, out["connected"].to<JsonObject>());
    serializeMemory(info.decoded, out["decoded"].to<JsonObject>());
    serializeMemory(info.released, out["released"].to<JsonObject>());
}
// Bounds both response bytes and stream timeout; JSON allocation is independently capped.
struct LimitedReader {
    Stream& stream;
    size_t remaining = MaxPayload + 1;
    int read() {
        if (!remaining) { return -1; }
        char value;
        if (stream.readBytes(&value, 1) != 1) { return -1; }
        --remaining; return static_cast<unsigned char>(value);
    }
    size_t readBytes(char* buffer, size_t length) {
        auto count = stream.readBytes(buffer, std::min(length, remaining)); remaining -= count; return count;
    }
};
// Keep the bridge's large HttpGetter/config frame out of native HTTPS call chains.
__attribute__((noinline)) bool normalizedGet(Settings const& settings, Domain domain, Series& series, char const*& error)
{
    HttpRequestConfig config {}; strncpy(config.Url, settings.url, sizeof(config.Url) - 1); config.Timeout = 3000;
    HttpGetter getter(config);
    if (!getter.init()) { error = "invalid_url"; return false; }
    auto request = getter.performGetRequest();
    if (!request || !request.getStream()) { error = "http_unavailable"; return false; }
    LimitedReader reader { *request.getStream() };
    Profiles::BoundedAllocator allocator(MaxJsonMemory, true); JsonDocument doc(&allocator);
    auto decoded = deserializeJson(doc, reader, DeserializationOption::NestingLimit(6));
    int c; bool trailing = false;
    while (!decoded && (c = reader.read()) >= 0) { if (c > 32) { trailing = true; break; } }
    if (decoded || !reader.remaining || trailing) { error = "malformed_or_oversized_json"; return false; }
    return parseSeries(doc.as<JsonVariantConst>(), domain, series, error);
}
// All request scratch dies here, before aggregation, quota delay, or the next plane.
__attribute__((noinline)) bool nativeRequest(Settings const& settings, unsigned plane, Memory::Ptr<Series>& series, NativeInfo& info, char const*& error)
{
    bool ok;
    {
        char url[256];
        if (settings.source == Source::ForecastSolar) { forecastUrl(settings.planes[plane], url, sizeof(url)); }
        else { tariffUrl(utcNow(), url, sizeof(url)); }
        Profiles::BoundedAllocator allocator(MaxJsonMemory, true); JsonDocument doc(&allocator);
        ok = providerGet(url, settings.source, doc, info, error);
        if (ok) {
            // providerGet has destroyed its HTTP/TLS objects. The destination is
            // only needed now; do not put another 5 KiB allocation below TLS.
            if (!series) { series = Memory::make<Series>(); }
            if (!series) { error = "insufficient_memory"; ok = false; }
            else {
                ok = settings.source == Source::ForecastSolar ? parseForecastSolar(doc.as<JsonVariantConst>(), *series, info, error)
                    : parseEnergyCharts(doc.as<JsonVariantConst>(), *series, error);
            }
        }
        info.jsonPeak = allocator.peak;
    }
    info.released = memorySample();
    Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Released, heapProvider(settings.source),
        settings.source == Source::ForecastSolar ? plane : -1);
    return ok;
}
constexpr uint32_t WorkerStackBytes = 10240;
} // namespace
Service EnergyData;
void Service::init(Scheduler& scheduler)
{
    auto file = LittleFS.open(Path, "r");
    if (file && file.size() <= 8192) {
        JsonDocument doc;
        Settings tariff, forecast;
        if (!deserializeJson(doc, file) && doc["version"].is<unsigned>() && doc["version"].as<unsigned>() == 1
                && parseSettings(doc["tariff"], tariff) && parseSettings(doc["forecast"], forecast)) {
            _tariff.configure(tariff); _forecast.configure(forecast);
        }
    }
    scheduler.addTask(_task);
    _task.setCallback([this] { loop(); }); _task.setInterval(1000); _task.setIterations(TASK_FOREVER); _task.enable();
}
bool Service::configure(Domain domain, Settings const& settings)
{
    Diagnostics::Scope activity(Diagnostics::Component::Configuration, Diagnostics::Operation::Write);
    if (settings.planeCount > MaxPlanes) { return false; }
    if ((domain == Domain::Tariff && settings.source == Source::ForecastSolar)
            || (domain == Domain::Forecast && settings.source == Source::EnergyCharts)) { return false; }
    std::lock_guard<std::mutex> lock(_mutex);
    JsonDocument doc;
    doc["version"] = 1;
    for (auto item : { Domain::Tariff, Domain::Forecast }) {
        serializeSettings(item == domain ? settings : provider(item).settings(), doc[name(item)].to<JsonObject>());
    }
    LittleFS.remove(Temp);
    auto file = LittleFS.open(Temp, "w");
    auto length = measureJson(doc);
    bool ok = !doc.overflowed() && length <= 8192 && file && serializeJson(doc, file) == length;
    if (file) { file.flush(); file.close(); }
    auto verify = LittleFS.open(Temp, "r");
    JsonDocument checked;
    Settings tariff, forecast;
    ok = ok && verify && verify.size() == length && !deserializeJson(checked, verify)
        && checked["version"].as<unsigned>() == 1 && parseSettings(checked["tariff"], tariff) && parseSettings(checked["forecast"], forecast);
    verify.close();
    if (!ok || !LittleFS.rename(Temp, Path)) { LittleFS.remove(Temp); return false; }
    provider(domain).configure(settings); ++_revision; _failures[static_cast<unsigned>(domain)] = 0;
    if (domain == Domain::Forecast) { for (auto& plane : _planes) { plane = {}; } }
    ++_generation[static_cast<unsigned>(domain)];
    _pending[static_cast<unsigned>(domain)] = false;
    return true;
}
uint32_t Service::revision() const { std::lock_guard<std::mutex> lock(_mutex); return _revision; }
void Service::background(bool (*due)(void*), void (*run)(void*), void* context)
{
    _backgroundDue = due; _backgroundRun = run; _backgroundContext = context;
}
void Service::snapshot(Domain domain, Series& series, Status& status) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto const& source = provider(domain); series = source.data(); status = source.status(utcNow(), millis());
}
void Service::serialize(Domain domain, JsonObject output, bool all) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto const& source = provider(domain); auto const& data = source.data(); auto now = utcNow();
    auto status = source.status(now, millis());
    serializeSettings(source.settings(), output["config"].to<JsonObject>());
    output["provider"] = sourceName(source.settings());
    output["current_covered"] = status.currentCovered; output["planning_usable"] = status.planningUsable;
    output["partial_horizon"] = status.partial; output["transport_age_seconds"] = status.transportAgeSeconds;
    output["source_age_seconds"] = nullptr;
    if (data.sourceTimestampKnown && status.hasData) { output["source_age_seconds"] = status.sourceAgeSeconds; }
    output["source_timestamp_known"] = data.sourceTimestampKnown;
    output["source_fresh"] = nullptr;
    if (data.sourceTimestampKnown && status.hasData) { output["source_fresh"] = status.sourceFresh; }
    output["transport_fresh"] = status.hasData && status.transportFresh;
    output["next_refresh"] = status.nextRefresh;
    auto decision = refreshDecision(domain);
    auto manual = output["manual_refresh"].to<JsonObject>();
    manual["allowed"] = static_cast<bool>(decision); manual["reason"] = decision.reason;
    manual["retry_after_seconds"] = decision.retryAfterSeconds;
    manual["next_allowed"] = nullptr;
    if (now >= MinUtc && (decision || decision.retryAfterSeconds)) { manual["next_allowed"] = now + decision.retryAfterSeconds; }
    output["request_stage"] = _stage[static_cast<unsigned>(domain)];
    auto const& native = _native[static_cast<unsigned>(domain)];
    output["worker_stack_bytes"] = WorkerStackBytes; output["worker_stack_free_bytes"] = _workerStackFree;
    nativeJson(native, output["request"].to<JsonObject>());
    auto memory = memorySample(); memory.stackFree = _workerStackFree;
    serializeMemory(memory, output["runtime_memory"].to<JsonObject>());
    output["revision"] = _revision; output["http_status"] = native.httpStatus; output["rate_limit"] = native.rateLimit; output["rate_remaining"] = native.rateRemaining;
    if (source.settings().source == Source::EnergyCharts) {
        output["attribution"] = "Energy-Charts.info; Bundesnetzagentur / SMARD.de; CC BY 4.0";
        output["region"] = "DE-LU";
    }
    if (source.settings().source == Source::ForecastSolar) {
        auto planes = output["plane_status"].to<JsonArray>();
        for (size_t i = 0; i < source.settings().planeCount; ++i) {
            auto p = planes.add<JsonObject>(); p["index"] = i;
            p["name"] = static_cast<char const*>(source.settings().planes[i].name);
            p["enabled"] = source.settings().planes[i].enabled;
            p["attempted_at"] = _planes[i].attemptedAt;
            p["state"] = !source.settings().planes[i].enabled ? "disabled" : !_planes[i].attemptedAt ? "not_attempted"
                : _planes[i].error ? "failed" : _planes[i].info.httpStatus == 200 ? "success" : "loading";
            p["fetched_at"] = _planes[i].fetchedAt;
            // These timestamps describe the last successful plane parse, which
            // need not have produced an accepted complete multi-plane aggregate.
            p["generated_at"] = _planes[i].generatedAt;
            p["provider_age_seconds"] = nullptr; p["transport_age_seconds"] = nullptr;
            if (_planes[i].generatedAt) { p["provider_age_seconds"] = now >= _planes[i].generatedAt ? now - _planes[i].generatedAt : 0; }
            if (_planes[i].fetchedAt) { p["transport_age_seconds"] = now >= _planes[i].fetchedAt ? now - _planes[i].fetchedAt : 0; }
            p["http_status"] = _planes[i].info.httpStatus; p["last_error"] = _planes[i].error; p["rate_remaining"] = _planes[i].info.rateRemaining;
            p["rate_limit"] = _planes[i].info.rateLimit;
            nativeJson(_planes[i].info, p["request"].to<JsonObject>());
        }
    }
    output["schema"] = schema(domain); output["state"] = status.state; output["enabled"] = status.enabled;
    output["source"] = static_cast<char const*>(data.source); output["fresh"] = status.fresh;
    output["valid"] = status.hasData; output["horizon_complete"] = status.complete;
    output["last_error"] = status.error;
    output["fetched_at"] = nullptr; output["generated_at"] = nullptr; output["horizon_end"] = nullptr;
    if (status.fetchedAt) { output["fetched_at"] = status.fetchedAt; }
    if (status.sourceAt) { output["generated_at"] = status.sourceAt; }
    if (status.horizonEnd) { output["horizon_end"] = status.horizonEnd; }
    output["age_seconds"] = status.ageSeconds; output["interval_count"] = data.count;
    output["future_count"] = status.futureCount;
    output["unit"] = domain == Domain::Tariff ? "micro_currency/kWh" : "W";
    if (domain == Domain::Tariff) { output["currency"] = static_cast<char const*>(data.currency); }
    output["current"] = nullptr;
    auto current = data.current(now);
    if (current) { intervalJson(domain, *current, output["current"].to<JsonObject>()); }
    auto intervals = output["intervals"].to<JsonArray>();
    for (uint16_t i = 0; i < data.count; ++i) {
        if (all || (data.intervals[i].end > now && intervals.size() < 8)) {
            intervalJson(domain, data.intervals[i], intervals.add<JsonObject>());
        }
    }
}
bool Service::ingest(Domain domain, JsonVariantConst payload)
{
    auto series = Memory::make<Series>();
    char const* error = nullptr;
    bool parsed = series && parseSeries(payload, domain, *series, error);
    std::lock_guard<std::mutex> lock(_mutex);
    auto& source = provider(domain);
    if (!parsed) { source.failed(error ? error : "insufficient_memory", millis()); return false; }
    if (pollable(source.settings())) { source.failed("http_provider_rejects_push", millis()); return false; }
    bool ok = source.accept(*series, utcNow(), millis()); if (ok) { ++_revision; } return ok;
}
char const* Service::prerequisite(Domain domain) const
{
    auto const& s = provider(domain).settings();
    if (!s.enabled) { return "not_enabled"; }
    if (!pollable(s) || (s.source == Source::ForecastSolar && (!s.planeCount || s.planeCount > MaxPlanes))) { return "invalid_config"; }
    if (s.source == Source::ForecastSolar) {
        bool enabled = false; for (unsigned i = 0; i < s.planeCount; ++i) { enabled = enabled || s.planes[i].enabled; }
        if (!enabled) { return "invalid_config"; }
    }
    if (!clockReady(s)) { return "clock_invalid"; }
    if (!NetworkSettings.isConnected()) { return "network_unavailable"; }
    return nullptr;
}
RefreshDecision Service::refreshDecision(Domain domain) const
{
    if (_pending[0] || _pending[1] || _running[0] || _running[1]) { return { "busy", 0 }; } // Completion time is unknown.
    if (auto reason = prerequisite(domain)) { return { reason, 0 }; }
    return provider(domain).retryDecision(millis());
}
RefreshDecision Service::requestRefresh(Domain domain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto result = refreshDecision(domain);
    if (result) { _pending[static_cast<unsigned>(domain)] = true; }
    return result;
}
Status Service::status(Domain domain) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return provider(domain).status(time(nullptr), millis());
}
void Service::loop()
{
    bool needed = _backgroundDue && _backgroundDue(_backgroundContext);
    needed = (_diagnosticsDue && _diagnosticsDue(_diagnosticsContext)) || needed;
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto domain : { Domain::Tariff, Domain::Forecast }) {
        auto i = static_cast<unsigned>(domain);
        auto& source = provider(domain);
        if (!_running[i] && !_pending[i] && source.refreshDue(millis())) {
            if (auto reason = prerequisite(domain)) {
                source.failed(reason, millis()); _native[i] = {}; _stage[i] = "local_prerequisite";
            } else { _pending[i] = true; }
        }
        needed = needed || _pending[i];
    }
    if (!needed || (!_worker && _workerUnavailable && millis() - _workerFailureMs < 5000)) { return; }
    // Sized from measured ESP32-S3 frames and the 108-byte hardware watermark.
    // See docs/HEMS_Runtime_Hardening.md; hardware must confirm >=2 KiB free.
    if (!_worker && xTaskCreate(workerEntry, "hems-energy", WorkerStackBytes, this, 1, &_worker) != pdPASS) {
        _worker = nullptr; _workerUnavailable = true; _workerFailureMs = millis(); _pending[0] = _pending[1] = false;
        for (auto domain : { Domain::Tariff, Domain::Forecast }) { provider(domain).failed("worker_unavailable", millis()); }
        return;
    }
    _workerUnavailable = false;
    xTaskNotifyGive(_worker);
}
void Service::workerEntry(void* context)
{
    auto& service = *static_cast<Service*>(context);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (service._backgroundRun) { service._backgroundRun(service._backgroundContext); }
        for (auto domain : { Domain::Tariff, Domain::Forecast }) { service.runPending(domain); }
        if (service._diagnosticsRun) { service._diagnosticsRun(service._diagnosticsContext); }
        // fetch/runPending frames, unpublished Series and optimizer scratch are gone.
        Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::WorkerIdle);
        auto freeStack = uxTaskGetStackHighWaterMark(nullptr); // ESP-IDF reports bytes.
        { std::lock_guard<std::mutex> lock(service._mutex); service._workerStackFree = freeStack; }
    }
}
// Settings must be a consistent generation snapshot, but need not sit below optimizer work.
__attribute__((noinline)) void Service::runPending(Domain domain)
{
    Settings settings; uint32_t generation;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto i = static_cast<unsigned>(domain);
        if (!_pending[i]) { return; }
        _pending[i] = false; generation = _generation[i];
        settings = provider(domain).settings(); _running[i] = true;
        _native[i] = {}; _stage[i] = "local_prerequisite";
        if (domain == Domain::Forecast) {
            // A new cycle must not present an unattempted plane's old error or
            // transport result as the result of this refresh. Keep last success.
            for (auto& plane : _planes) { plane.attemptedAt = 0; plane.error = nullptr; plane.info = {}; }
        }
    }
    Diagnostics::Scope activity(Diagnostics::Component::Energy, domain == Domain::Tariff ? Diagnostics::Operation::Tariff : Diagnostics::Operation::Forecast);
    fetch(domain, settings, generation);
    std::lock_guard<std::mutex> lock(_mutex); _running[static_cast<unsigned>(domain)] = false;
}
void Service::fetch(Domain domain, Settings const& settings, uint32_t generation)
{
    char const* error = nullptr;
    Memory::Ptr<Series> series;
    bool parsed = false;
    bool outbound = false, response = false;
    if (!clockReady(settings)) { error = "clock_invalid"; }
    else if (!NetworkSettings.isConnected()) { error = "network_unavailable"; }
    else if (settings.source != Source::Normalized) {
        NativeInfo info;
        unsigned requests = settings.source == Source::ForecastSolar ? settings.planeCount : 1;
        parsed = true;
        for (unsigned i = 0; i < requests; ++i) {
            if (settings.source == Source::ForecastSolar && !settings.planes[i].enabled) { continue; }
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_generation[static_cast<unsigned>(domain)] != generation) { return; }
                if (settings.source == Source::ForecastSolar) { _planes[i].attemptedAt = utcNow(); }
            }
            response = false;
            Diagnostics::workerPhase(Diagnostics::Phase::ProviderStart, settings.source == Source::ForecastSolar ? i : -1);
            bool merge = settings.source == Source::ForecastSolar && series && series->count;
            Memory::Ptr<Series> single; // Dies before quota delay and the next HTTPS request.
            info = {};
            bool ok = nativeRequest(settings, i, merge ? single : series, info, error);
            {
                std::lock_guard<std::mutex> lock(_mutex);
                outbound = outbound || info.dns >= 0 || info.httpStatus != 0;
                response = info.httpStatus > 0;
                // An obsolete result cannot publish data, but requests already made
                // still count for request spacing and server/quota holds.
                if (provider(domain).settings().source == settings.source) {
                    if (info.dns >= 0 || info.httpStatus != 0) {
                        provider(domain).cooldown(millis(), settings.source == Source::EnergyCharts ? 30 : 15, RetryKind::Transport);
                    }
                    if (info.retrySeconds) { provider(domain).defer(millis(), info.retrySeconds, info.httpStatus == 429 || info.rateRemaining == 0 || (error && !strcmp(error, "provider_rate_limited")) ? RetryKind::RateLimited : RetryKind::Provider); }
                }
                if (_generation[static_cast<unsigned>(domain)] != generation) { return; }
                _stage[static_cast<unsigned>(domain)] = info.httpStatus > 0 ? "http_response" : info.dns == 0 ? "dns" : outbound ? "transport" : "local_prerequisite";
                _native[static_cast<unsigned>(domain)] = info;
                if (settings.source == Source::ForecastSolar) {
                    auto& plane = _planes[i];
                    if (ok) { plane.fetchedAt = utcNow(); plane.generatedAt = (merge ? *single : *series).generatedAt; }
                    plane.error = error; plane.info = info;
                }
            }
            if (!ok) { parsed = false; break; }
            if (merge) {
                Diagnostics::workerPhase(Diagnostics::Phase::Aggregate, i);
                if (!aggregate(*series, *single, *series, error)) {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_generation[static_cast<unsigned>(domain)] != generation) { return; }
                    _planes[i].error = error;
                    parsed = false; break;
                }
            }
            single.reset();
            if (settings.source == Source::ForecastSolar && i + 1 < requests) {
                bool more = false;
                for (unsigned j = i + 1; j < requests; ++j) { more = more || settings.planes[j].enabled; }
                if (more && info.rateRemaining == 0) { parsed = false; error = "provider_rate_limited"; break; }
                if (more && info.retrySeconds) { parsed = false; error = "provider_backoff"; break; }
                if (more) { vTaskDelay(pdMS_TO_TICKS(5000)); }
            }
        }
    } else {
        series = Memory::make<Series>();
        if (!series) { error = "insufficient_memory"; }
        else { outbound = true; parsed = normalizedGet(settings, domain, *series, error); }
    }
    std::lock_guard<std::mutex> lock(_mutex);
    if (_generation[static_cast<unsigned>(domain)] != generation) { return; }
    auto index = static_cast<unsigned>(domain);
    if (parsed && provider(domain).accept(*series, utcNow(), millis())) {
        ++_revision; _failures[index] = 0; _stage[index] = "success";
        provider(domain).cooldown(millis(), settings.source == Source::EnergyCharts ? 30 : 15, RetryKind::Transport);
    } else {
        if (parsed) { error = provider(domain).lastError(); }
        provider(domain).failed(error ? error : "invalid_payload", millis());
        if (response) { provider(domain).defer(millis(), 300); }
        else if (outbound) {
            _failures[index] = std::min(unsigned(_failures[index]) + 1, 6U);
            provider(domain).cooldown(millis(), std::min(15U << (_failures[index] - 1), 300U), RetryKind::Transport);
        }
    }
}
} // namespace Hems::Energy
