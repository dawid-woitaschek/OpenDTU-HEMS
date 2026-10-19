// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <esp_system.h>
namespace Diagnostics {
inline char const* resetName(esp_reset_reason_t value)
{
    switch (value) {
#define RESET_NAME(v) case v: return #v
    RESET_NAME(ESP_RST_UNKNOWN); RESET_NAME(ESP_RST_POWERON); RESET_NAME(ESP_RST_EXT);
    RESET_NAME(ESP_RST_SW); RESET_NAME(ESP_RST_PANIC); RESET_NAME(ESP_RST_INT_WDT);
    RESET_NAME(ESP_RST_TASK_WDT); RESET_NAME(ESP_RST_WDT); RESET_NAME(ESP_RST_DEEPSLEEP);
    RESET_NAME(ESP_RST_BROWNOUT); RESET_NAME(ESP_RST_SDIO);
#undef RESET_NAME
    default: return "ESP_RST_UNRECOGNIZED";
    }
}
inline bool mayRetain(esp_reset_reason_t value)
{
    // Power/brownout/unknown must not resurrect an apparently intact stale record.
    return value == ESP_RST_SW || value == ESP_RST_PANIC || value == ESP_RST_INT_WDT
        || value == ESP_RST_TASK_WDT || value == ESP_RST_WDT || value == ESP_RST_DEEPSLEEP;
}
} // namespace Diagnostics
