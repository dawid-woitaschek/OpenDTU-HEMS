// SPDX-License-Identifier: GPL-2.0-or-later
#include <RuntimeDiagnostics.h>
#if defined(ESP32)
#include <diagnostics/ResetReason.h>
#include <diagnostics/Serialize.h>
#include <diagnostics/HeapSerialize.h>
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
namespace Diagnostics {
namespace {
RTC_NOINIT_ATTR Storage retained;
Journal journal;
HeapJournal heapJournal;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool shutdownRegistered = false;
bool allocationHookRegistered = false;
#if defined(HEMS_DEBUG_HEAP_INTEGRITY) && HEMS_DEBUG_HEAP_INTEGRITY
uint32_t integrityFailures = 0;
#endif
struct Lock { Lock() { portENTER_CRITICAL(&mux); } ~Lock() { portEXIT_CRITICAL(&mux); } };
void shutdown() { Lock lock; journal.espRestart(); }
// All callers hold mux. Prefer the shared HEMS worker while it is active;
// unrelated configuration/network activity must not hide its operation.
HeapContext heapContext()
{
    HeapContext context;
    auto const* activity = journal.current.activities[0].active ? &journal.current.activities[0] : selected(journal.current);
    if (activity) {
        context.component = activity->component; context.operation = activity->operation;
        context.phase = activity->phase; context.subIndex = activity->subIndex;
    }
    return context;
}
void allocationFailed(size_t requested, uint32_t caps, char const* function)
{
    // No allocation, logging, JSON, stack scan or heap traversal here. The
    // allocator's failure path can also run in ISR context. Both the hook and
    // normal diagnostics use the same short scalar-copy critical section.
    auto uptime = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    portENTER_CRITICAL_SAFE(&mux);
    heapJournal.failed(requested, caps, function, uptime, heapContext());
    portEXIT_CRITICAL_SAFE(&mux);
}
void serializeHeap(JsonObject out, bool history)
{
    decltype(heapJournal.samples)::Cursor samples;
    decltype(heapJournal.failures)::Cursor failures;
    { Lock lock; samples = heapJournal.samples.cursor(); failures = heapJournal.failures.cursor(); }
    out["failed_alloc_hook_registered"] = allocationHookRegistered;
    out["history_capacity"] = 16; out["failure_capacity"] = 4;
    out["checkpoint_count"] = samples.sequence; out["failed_alloc_count"] = failures.sequence;
    out["static_bytes"] = sizeof(heapJournal);
    bool overwritten = false;
    if (history && failures.count) {
        auto rows = out["failed_allocations"].to<JsonArray>();
        for (unsigned i = 0; i < failures.count; ++i) {
            HeapFailure value;
            bool present;
            { Lock lock; present = heapJournal.failures.read(failures.sequence - failures.count + 1 + i, value); }
            if (present) { heapFailureJson(value, rows.add<JsonObject>()); }
            else { overwritten = true; }
        }
    }
    if (history) {
        HeapSample boot; { Lock lock; boot = heapJournal.boot; }
        heapSampleJson(boot, out["boot_baseline"].to<JsonObject>());
        auto rows = out["history"].to<JsonArray>();
        for (unsigned i = 0; i < samples.count; ++i) {
            HeapSample value;
            bool present;
            { Lock lock; present = heapJournal.samples.read(samples.sequence - samples.count + 1 + i, value); }
            if (present) { heapSampleJson(value, rows.add<JsonObject>()); }
            else { overwritten = true; }
        }
    }
    out["overwritten_during_serialization"] = overwritten;
#if defined(HEMS_DEBUG_HEAP_INTEGRITY) && HEMS_DEBUG_HEAP_INTEGRITY
    uint32_t count; { Lock lock; count = integrityFailures; }
    out["debug_heap_integrity_failures"] = count;
#endif
}
} // namespace
void init()
{
    journal.boot(retained, mayRetain(esp_reset_reason()));
    shutdownRegistered = esp_register_shutdown_handler(shutdown) == ESP_OK;
    journal.hookRegistered(shutdownRegistered);
    allocationHookRegistered = heap_caps_register_failed_alloc_callback(allocationFailed) == ESP_OK;
    heapCheckpoint(HeapPoint::Boot);
}
uint32_t begin(Component c, Operation op, Phase p, int8_t index) { Lock lock; return journal.begin(c, op, p, index, millis()); }
void phase(uint32_t token, Phase p, int8_t index) { Lock lock; journal.phase(token, p, index, millis()); }
void finish(uint32_t token) { Lock lock; journal.finish(token, millis()); }
void workerPhase(Phase p, int8_t index)
{
    Lock lock; auto const& worker = journal.current.activities[0];
    if (worker.active) { journal.phase(worker.sequence, p, index, millis()); }
}
void restartRequested(RestartSource source) { Lock lock; journal.restartRequested(source, millis()); }
void intentionalRestart() { Lock lock; journal.intentionalRestart(millis()); }
void heapCheckpoint(HeapPoint point, HeapProvider provider, int8_t plane)
{
    // Heap walks and stack-watermark scans happen in the worker/calling task,
    // never in the failed-allocation hook or with the diagnostics lock held.
    constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#if defined(HEMS_DEBUG_HEAP_INTEGRITY) && HEMS_DEBUG_HEAP_INTEGRITY
    if (!heap_caps_check_integrity(caps, true)) { Lock lock; ++integrityFailures; }
#endif
    multi_heap_info_t info;
    heap_caps_get_info(&info, caps);
    HeapSample sample;
    sample.uptimeMs = millis(); sample.freeInternal = info.total_free_bytes;
    sample.minimumInternal = info.minimum_free_bytes; sample.largestInternal = info.largest_free_block;
    sample.allocatedInternal = info.total_allocated_bytes;
    sample.freeBlocks = info.free_blocks; sample.allocatedBlocks = info.allocated_blocks;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    sample.freePsram = info.total_free_bytes; sample.minimumPsram = info.minimum_free_bytes;
    sample.largestPsram = info.largest_free_block; sample.allocatedPsram = info.total_allocated_bytes;
    sample.freePsramBlocks = info.free_blocks; sample.allocatedPsramBlocks = info.allocated_blocks;
    sample.stackFree = uxTaskGetStackHighWaterMark(nullptr);
    Lock lock;
    sample.context = heapContext(); sample.context.point = point; sample.context.provider = provider;
    sample.context.plane = plane >= 0 ? plane : provider == HeapProvider::ForecastSolar ? sample.context.subIndex : -1;
    if (point == HeapPoint::Boot) { sample.context.component = Component::Runtime; sample.context.operation = Operation::Boot; }
    heapJournal.sample(sample);
}
void serialize(JsonObject out, bool heapHistory)
{
    // No JSON allocation while interrupts are disabled.
    Record current; { Lock lock; current = journal.current; }
    recordJson(journal.previous, journal.previousValid, out["previous_boot"].to<JsonObject>());
    recordJson(current, true, out["current_boot"].to<JsonObject>());
    out["esp_reset_reason"] = resetName(esp_reset_reason());
    out["esp_reset_reason_code"] = static_cast<int>(esp_reset_reason());
    out["rtc_retention"] = esp_reset_reason() == ESP_RST_WDT ? "best_effort" : esp_reset_reason() == ESP_RST_DEEPSLEEP ? "conditional" : mayRetain(esp_reset_reason()) ? "expected" : "discarded";
    out["shutdown_hook_registered"] = shutdownRegistered;
    serializeHeap(out["heap"].to<JsonObject>(), heapHistory);
}
} // namespace Diagnostics
#endif
