// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
inline uint32_t testNow = 1000;
inline uint32_t millis() { return testNow; }
inline void delay(unsigned ms) { testNow += ms; }
