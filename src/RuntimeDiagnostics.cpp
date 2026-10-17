// SPDX-License-Identifier: GPL-2.0-or-later
#include <RuntimeDiagnostics.h>
#if defined(ESP32)
#include <diagnostics/ResetReason.h>
#include <diagnostics/Serialize.h>
#include <Arduino.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
namespace Diagnostics {
namespace {
RTC_NOINIT_ATTR Storage retained;
Journal journal;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool shutdownRegistered = false;
struct Lock { Lock() { portENTER_CRITICAL(&mux); } ~Lock() { portEXIT_CRITICAL(&mux); } };
void shutdown() { Lock lock; journal.espRestart(); }

}
void init()
{
    journal.boot(retained, mayRetain(esp_reset_reason()));
    shutdownRegistered = esp_register_shutdown_handler(shutdown) == ESP_OK;
    journal.hookRegistered(shutdownRegistered);
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
void serialize(JsonObject out)
{
    // No JSON allocation while interrupts are disabled.
    Record current; { Lock lock; current = journal.current; }
    recordJson(journal.previous, journal.previousValid, out["previous_boot"].to<JsonObject>());
    recordJson(current, true, out["current_boot"].to<JsonObject>());
    out["esp_reset_reason"] = resetName(esp_reset_reason());
    out["esp_reset_reason_code"] = static_cast<int>(esp_reset_reason());
    out["rtc_retention"] = esp_reset_reason() == ESP_RST_WDT ? "best_effort" : mayRetain(esp_reset_reason()) ? "expected" : "discarded";
    out["shutdown_hook_registered"] = shutdownRegistered;
}
} // namespace Diagnostics
#endif
