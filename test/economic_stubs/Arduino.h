// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../provider_stubs/Arduino.h"
inline unsigned long micros() { return testNow * 1000; }
