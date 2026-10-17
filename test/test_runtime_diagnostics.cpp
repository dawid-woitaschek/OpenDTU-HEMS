// SPDX-License-Identifier: GPL-2.0-or-later
#include <diagnostics/Breadcrumb.h>
#include <diagnostics/ResetReason.h>
#include <diagnostics/Serialize.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace Diagnostics;
int main()
{
    Storage storage; memset(&storage, 0xa5, sizeof(storage));
    Journal first; first.boot(storage, true); assert(!first.previousValid && valid(first.current));
    assert(first.current.generation == 1 && !selected(first.current));
    first.hookRegistered(true);
    auto worker = first.begin(Component::Energy, Operation::Forecast, Phase::ProviderStart, 1, 12000);
    first.phase(worker, Phase::Dns, -1, 12001); assert(selected(first.current)->subIndex == 1);
    first.phase(worker, Phase::Connect, -1, 12002);
    auto network = first.begin(Component::Network, Operation::Reconnect, Phase::Start, -1, 12003);
    first.finish(network, 12004);
    assert(selected(first.current)->component == Component::Energy); // Completed unrelated work cannot hide a live worker.
    Journal second; second.boot(storage, true);
    assert(second.previousValid && second.previous.shutdownRegistered && second.previous.generation == 1 && second.current.generation == 2);
    auto interrupted = selected(second.previous);
    assert(interrupted && interrupted->active && interrupted->phase == Phase::Connect && interrupted->subIndex == 1 && interrupted->uptimeMs == 12002);
    JsonDocument json; recordJson(second.previous, second.previousValid, json.to<JsonObject>());
    assert(json["active"] == true && json["component"] == "energy" && json["operation"] == "forecast" && json["sub_index"] == 1);
    auto optimizer = second.begin(Component::Optimizer, Operation::Calculate, Phase::Start, -1, 13000);
    second.finish(worker, 13001); assert(selected(second.current)->active); // Stale token cannot complete another operation.
    second.finish(optimizer, 13002); assert(!selected(second.current)->active);
    second.restartRequested(RestartSource::MaintenanceReboot, 14000);
    assert(second.current.restartRequested && !second.current.intentionalRestart && !second.current.espRestartCalled);
    second.intentionalRestart(15000); second.espRestart();
    Journal third; third.boot(storage, true);
    assert(third.previousValid && third.previous.intentionalRestart && third.previous.espRestartCalled && third.previous.restartRequested);
    assert(third.previous.restartSource == RestartSource::MaintenanceReboot && third.previous.restartAt == 15000);
    json.clear(); recordJson(third.previous, true, json.to<JsonObject>());
    assert(json["intentional_restart"] == true && json["restart_source"] == "maintenance_reboot" && json["active"] == false);
    for (auto source : {RestartSource::FirmwareUpdate, RestartSource::ConfigurationReset, RestartSource::FileRestore, RestartSource::PinMappingChange, RestartSource::InverterUnresponsive, RestartSource::Unspecified}) {
        third.restartRequested(source, 16000); third.intentionalRestart(16001);
        Journal reboot; reboot.boot(storage, true); assert(reboot.previousValid && reboot.previous.restartSource == source); third = reboot;
    }
    Journal direct; direct.boot(storage, true); direct.hookRegistered(true); direct.espRestart();
    Journal directBoot; directBoot.boot(storage, true);
    assert(directBoot.previous.espRestartCalled && !directBoot.previous.intentionalRestart && !directBoot.previous.restartRequested);
    // Torn newest write falls back to the last committed record; corrupt both and reject safely.
    auto torn = storage; unsigned newest = int32_t(torn.slots[1].sequence - torn.slots[0].sequence) > 0 ? 1 : 0;
    torn.slots[newest].magic = 0;
    Journal fallback; fallback.boot(torn, true); assert(fallback.previousValid);
    torn.slots[0].checksum ^= 1; torn.slots[1].checksum ^= 1;
    Journal corrupt; corrupt.boot(torn, true); assert(!corrupt.previousValid);
    Journal powerOn; powerOn.boot(storage, false); assert(!powerOn.previousValid && powerOn.current.generation == 1);
    json.clear(); recordJson(powerOn.previous, false, json.to<JsonObject>()); assert(json["valid"] == false && json.size() == 1);
    // Reset immediately after a cold boot's first commit: an older warm-reset
    // slot must not supersede the new session before another checkpoint.
    Journal immediateReset; immediateReset.boot(storage, true);
    assert(immediateReset.previousValid && immediateReset.previous.generation == powerOn.current.generation
        && immediateReset.previous.sequence == powerOn.current.sequence);
    for (auto reason : {ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP}) { assert(mayRetain(reason)); }
    for (auto reason : {ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_BROWNOUT, ESP_RST_SDIO}) { assert(!mayRetain(reason)); }
    assert(!strcmp(resetName(ESP_RST_PANIC), "ESP_RST_PANIC") && !strcmp(resetName(ESP_RST_SW), "ESP_RST_SW"));
    assert(!strcmp(resetName(ESP_RST_INT_WDT), "ESP_RST_INT_WDT") && !strcmp(resetName(ESP_RST_TASK_WDT), "ESP_RST_TASK_WDT"));
    assert(!strcmp(resetName(static_cast<esp_reset_reason_t>(99)), "ESP_RST_UNRECOGNIZED"));
    std::cout << "RTC journal corruption/torn-write handling, previous boot, active/completed/concurrent operations, restart markers and SDK reason mapping passed; retained bytes=" << sizeof(Storage) << '\n';
}
