// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vector>
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
inline std::vector<uint32_t> delays;
inline void vTaskDelay(uint32_t ticks) { delays.push_back(ticks); if (duringDelay) { auto callback = std::move(duringDelay); duringDelay = {}; callback(); } }

inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }
