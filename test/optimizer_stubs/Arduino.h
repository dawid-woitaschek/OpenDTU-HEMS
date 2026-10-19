// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
inline uint32_t testNow = 0;
inline unsigned long millis() { return testNow; }
inline unsigned long micros() { return testNow * 1000; }
