// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <freertos/FreeRTOS.h>
struct TestTask;
using TaskHandle_t = TestTask*;
int xTaskCreate(void (*entry)(void*), char const*, unsigned, void*, int, TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, uint32_t);
