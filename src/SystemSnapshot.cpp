// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2026 Thomas Basler and others
 */
#include <RuntimeDiagnostics.h>
#include <SystemSnapshot.h>
#include <soc/soc_caps.h>
#include <Configuration.h>
#include <PinMapping.h>
#include <__compiled_constants.h>
#include <CpuTemperature.h>
#include <Hoymiles.h>
#include <LittleFS.h>
#include <NetworkSettings.h>
#include <ResetReason.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <array>

void systemSnapshot(JsonObject root, bool heapHistory)
{
    root["sdkversion"] = ESP.getSdkVersion();
    root["cpufreq"] = ESP.getCpuFreqMHz();
    root["cputemp"] = CpuTemperature.read();

    root["heap_total"] = ESP.getHeapSize();
    root["heap_used"] = ESP.getHeapSize() - ESP.getFreeHeap();
    root["heap_max_block"] = ESP.getMaxAllocHeap();
    root["heap_min_free"] = ESP.getMinFreeHeap();

    root["psram_total"] = ESP.getPsramSize();
    root["psram_used"] = ESP.getPsramSize() - ESP.getFreePsram();
    root["psram_max_block"] = ESP.getMaxAllocPsram();
    root["psram_min_free"] = ESP.getMinFreePsram();

    root["sketch_total"] = ESP.getFreeSketchSpace();
    root["sketch_used"] = ESP.getSketchSize();
    root["littlefs_total"] = LittleFS.totalBytes();
    root["littlefs_used"] = LittleFS.usedBytes();

    root["chiprevision"] = ESP.getChipRevision();
    root["chipmodel"] = ESP.getChipModel();
    root["chipcores"] = ESP.getChipCores();
    root["flashsize"] = ESP.getFlashChipSize();

    JsonArray taskDetails = root["task_details"].to<JsonArray>();
    static std::array<char const*, 16> constexpr task_names = {
        "hems-energy", "IDLE0", "IDLE1", "wifi", "tiT", "loopTask", "async_tcp", "mqttclient",
        "HuaweiHwIfc", "HuaweiTwai", "HuaweiMCP2515",
        "TruckiPolling",
        "PM:SDM", "PM:HTTP+JSON", "PM:SML", "PM:HTTP+SML",
    };
    for (char const* task_name : task_names) {
        TaskHandle_t const handle = xTaskGetHandle(task_name);
        if (!handle) {
            continue;
        }
        JsonObject task = taskDetails.add<JsonObject>();
        task["name"] = task_name;
        task["stack_watermark"] = uxTaskGetStackHighWaterMark(handle);
        task["priority"] = uxTaskPriorityGet(handle);
    }

    String reason;
    reason = ResetReason::get_reset_reason_verbose(0);
    root["resetreason_0"] = reason;

    reason = ResetReason::get_reset_reason_verbose(1);
    root["resetreason_1"] = reason;
    root["resetreason_0_raw"] = ResetReason::get_reset_reason_raw(0);
#if SOC_CPU_CORES_NUM > 1
    root["resetreason_1_raw"] = ResetReason::get_reset_reason_raw(1);
#else
    root["resetreason_1_raw"] = nullptr;
#endif
    Diagnostics::serialize(root["reset_diagnostics"].to<JsonObject>(), heapHistory);

    char version[16];
    snprintf(version, sizeof(version), "%d.%d.%d", CONFIG_VERSION >> 24 & 0xff, CONFIG_VERSION >> 16 & 0xff, CONFIG_VERSION >> 8 & 0xff);
    root["config_version"] = version;
    root["config_version_onbattery"] = CONFIG_VERSION_ONBATTERY;
    root["git_hash"] = __COMPILED_GIT_HASH__;
    root["source_commit"] = __COMPILED_SOURCE_COMMIT__;
    root["tested_merge_commit"] = __COMPILED_TESTED_MERGE_COMMIT__;
    root["git_sha"] = __COMPILED_GIT_SHA__;
    root["update_repository"] = __COMPILED_UPDATE_REPOSITORY__;
    root["update_channel"] = __COMPILED_UPDATE_CHANNEL__;
    root["git_branch"] = __COMPILED_GIT_BRANCH__;
    root["pioenv"] = PIOENV;

    root["uptime"] = esp_timer_get_time() / 1000000;

    root["nrf_configured"] = PinMapping.isValidNrf24Config();
    root["nrf_connected"] = Hoymiles.getRadioNrf()->isConnected();
    root["nrf_pvariant"] = Hoymiles.getRadioNrf()->isPVariant();

    root["cmt_configured"] = PinMapping.isValidCmt2300Config();
    root["cmt_connected"] = Hoymiles.getRadioCmt()->isConnected();

    root["network"]["connected"] = NetworkSettings.isConnected();
    root["network"]["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    if (WiFi.status() == WL_CONNECTED) { root["network"]["rssi_dbm"] = WiFi.RSSI(); }
}
void currentHeapSnapshot(JsonObject out)
{
    for (auto psram : {false, true}) {
        auto caps = MALLOC_CAP_8BIT | (psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
        multi_heap_info_t info {}; heap_caps_get_info(&info, caps);
        auto row = out[psram ? "psram" : "internal"].to<JsonObject>();
        row["total_bytes"] = heap_caps_get_total_size(caps); row["free_bytes"] = info.total_free_bytes;
        row["minimum_free_bytes"] = info.minimum_free_bytes; row["largest_free_block"] = info.largest_free_block;
        row["allocated_bytes"] = info.total_allocated_bytes; row["free_blocks"] = info.free_blocks;
        row["allocated_blocks"] = info.allocated_blocks;
        row["fragmentation_percent"] = Diagnostics::fragmentationPercent(info.total_free_bytes, info.largest_free_block);
    }
}
