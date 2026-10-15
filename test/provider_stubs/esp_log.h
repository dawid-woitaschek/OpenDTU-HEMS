// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
template<typename... Args> void testLog(Args const&...) {}
#define ESP_LOGI(...) testLog(__VA_ARGS__)
#define ESP_LOGW(...) testLog(__VA_ARGS__)
