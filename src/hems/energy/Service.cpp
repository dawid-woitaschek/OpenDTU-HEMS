// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/energy/Service.h>
#include <hems/profiles/Profile.h>
#include <HttpGetter.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
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
uint32_t utcNow() { auto now = time(nullptr); return now > 0 && static_cast<uint64_t>(now) <= UINT32_MAX ? now : 0; }
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
// Bounds both response bytes and stream timeout; JSON allocation is independently capped.
struct LimitedReader {
    Stream& stream;
    size_t remaining = MaxPayload;
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
} // namespace
Service EnergyData;
void Service::init(Scheduler& scheduler)
{
    auto file = LittleFS.open(Path, "r");
    if (file && file.size() <= 1536) {
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
    std::lock_guard<std::mutex> lock(_mutex);
    JsonDocument doc;
    doc["version"] = 1;
    for (auto item : { Domain::Tariff, Domain::Forecast }) {
        serializeSettings(item == domain ? settings : provider(item).settings(), doc[name(item)].to<JsonObject>());
    }
    LittleFS.remove(Temp);
    auto file = LittleFS.open(Temp, "w");
    auto length = measureJson(doc);
    bool ok = !doc.overflowed() && length <= 1536 && file && serializeJson(doc, file) == length;
    if (file) { file.flush(); file.close(); }
    auto verify = LittleFS.open(Temp, "r");
    JsonDocument checked;
    Settings tariff, forecast;
    ok = ok && verify && verify.size() == length && !deserializeJson(checked, verify)
        && checked["version"].as<unsigned>() == 1 && parseSettings(checked["tariff"], tariff) && parseSettings(checked["forecast"], forecast);
    verify.close();
    if (!ok || !LittleFS.rename(Temp, Path)) { LittleFS.remove(Temp); return false; }
    provider(domain).configure(settings); ++_generation[static_cast<unsigned>(domain)];
    _pending[static_cast<unsigned>(domain)] = false;
    return true;
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
    output["provider"] = source.settings().url[0] ? "normalized_http" : "normalized_push";
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
    auto series = std::unique_ptr<Series>(new (std::nothrow) Series());
    char const* error = nullptr;
    bool parsed = series && parseSeries(payload, domain, *series, error);
    std::lock_guard<std::mutex> lock(_mutex);
    auto& source = provider(domain);
    if (!parsed) { source.failed(error ? error : "insufficient_memory", millis()); return false; }
    if (source.settings().url[0]) { source.failed("http_provider_rejects_push", millis()); return false; }
    return source.accept(*series, utcNow(), millis());
}
bool Service::requestRefresh(Domain domain)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto const& settings = provider(domain).settings();
    if (!settings.enabled || !settings.url[0]) { return false; }
    _pending[static_cast<unsigned>(domain)] = true;
    return true;
}
void Service::loop()
{
    std::lock_guard<std::mutex> lock(_mutex);
    bool needed = false;
    for (auto domain : { Domain::Tariff, Domain::Forecast }) {
        auto i = static_cast<unsigned>(domain);
        auto& source = provider(domain);
        if (source.refreshDue(millis()) && utcNow() >= MinUtc) { _pending[i] = true; }
        needed = needed || _pending[i];
    }
    if (!needed) { return; }
    if (!_worker && xTaskCreate(workerEntry, "hems-energy", 4096, this, 1, &_worker) != pdPASS) {
        _worker = nullptr;
        for (auto domain : { Domain::Tariff, Domain::Forecast }) { provider(domain).failed("worker_unavailable", millis()); }
        return;
    }
    xTaskNotifyGive(_worker);
}
void Service::workerEntry(void* context)
{
    auto& service = *static_cast<Service*>(context);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (auto domain : { Domain::Tariff, Domain::Forecast }) {
            Settings settings; uint32_t generation;
            {
                std::lock_guard<std::mutex> lock(service._mutex);
                auto i = static_cast<unsigned>(domain);
                if (!service._pending[i]) { continue; }
                service._pending[i] = false; generation = service._generation[i];
                settings = service.provider(domain).settings(); service.provider(domain).attempted(millis());
            }
            service.fetch(domain, settings, generation);
        }
    }
}
void Service::fetch(Domain domain, Settings const& settings, uint32_t generation)
{
    char const* error = nullptr;
    auto series = std::unique_ptr<Series>(new (std::nothrow) Series());
    bool parsed = false;
    if (!series) { error = "insufficient_memory"; }
    else if (!NetworkSettings.isConnected()) { error = "network_unavailable"; }
    else {
        HttpRequestConfig config {}; strncpy(config.Url, settings.url, sizeof(config.Url) - 1); config.Timeout = 3000;
        HttpGetter getter(config);
        if (!getter.init()) { error = "invalid_url"; }
        else {
            auto request = getter.performGetRequest();
            if (!request || !request.getStream()) { error = "http_unavailable"; }
            else {
                LimitedReader reader { *request.getStream() };
                Profiles::BoundedAllocator allocator(MaxJsonMemory); JsonDocument doc(&allocator);
                auto decoded = deserializeJson(doc, reader, DeserializationOption::NestingLimit(6));
                if (decoded) { error = "malformed_or_oversized_json"; }
                else { parsed = parseSeries(doc.as<JsonVariantConst>(), domain, *series, error); }
            }
        }
    }
    std::lock_guard<std::mutex> lock(_mutex);
    if (_generation[static_cast<unsigned>(domain)] != generation) { return; }
    if (!parsed) { provider(domain).failed(error ? error : "invalid_payload", millis()); }
    else { provider(domain).accept(*series, utcNow(), millis()); }
}
} // namespace Hems::Energy
