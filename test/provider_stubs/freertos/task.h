// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <freertos/FreeRTOS.h>
#include <functional>
struct TestTask;
using TaskHandle_t = TestTask*;
int xTaskCreate(void (*entry)(void*), char const*, unsigned, void*, int, TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, uint32_t);

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(value) (value)
#endif
inline std::function<void()> duringDelay;
inline void vTaskDelay(uint32_t) { if (duringDelay) { auto callback = std::move(duringDelay); duringDelay = {}; callback(); } }

inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }
