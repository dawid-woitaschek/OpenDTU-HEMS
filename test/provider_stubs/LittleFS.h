// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
inline struct TestLittleFS {
    bool mounted = true;
    size_t totalBytes() const { return mounted ? 4096 : 0; }
} LittleFS;
