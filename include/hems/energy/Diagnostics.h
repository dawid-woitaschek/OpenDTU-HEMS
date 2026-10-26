// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#if defined(ESP32)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace Hems::Energy {
struct MemorySample {
    uint32_t freeInternal = 0, minimumInternal = 0, largestInternal = 0, stackFree = 0;
    uint32_t freePsram = 0, minimumPsram = 0, largestPsram = 0;
};
inline MemorySample memorySample()
{
#if defined(ESP32)
    constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    return { heap_caps_get_free_size(caps), heap_caps_get_minimum_free_size(caps),
        heap_caps_get_largest_free_block(caps), uxTaskGetStackHighWaterMark(nullptr),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) };
#else
    return {}; // Host tests cannot measure ESP32 memory; never synthesize device readings.
#endif
}
inline void serializeMemory(MemorySample const& sample, JsonObject out)
{
    out["free_internal_bytes"] = sample.freeInternal;
    out["minimum_internal_bytes"] = sample.minimumInternal; // Since boot, not per request.
    out["largest_internal_bytes"] = sample.largestInternal;
    out["free_psram_bytes"] = sample.freePsram;
    out["minimum_psram_bytes"] = sample.minimumPsram;
    out["largest_psram_bytes"] = sample.largestPsram;
    out["stack_free_bytes"] = sample.stackFree; // Calling task's minimum since creation.
}
} // namespace Hems::Energy
