// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
inline uint32_t transportNow = 0;
inline uint32_t millis() { return transportNow; }
inline void delay(unsigned ms) { transportNow += ms; }
