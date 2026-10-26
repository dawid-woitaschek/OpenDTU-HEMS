// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
constexpr unsigned MALLOC_CAP_8BIT = 4, MALLOC_CAP_SPIRAM = 1024;
inline bool hasPsram = true, memoryFailure = false;
inline unsigned lastCaps = 0, attempts = 0;
inline size_t heap_caps_get_total_size(unsigned) { return hasPsram ? 2097152 : 0; }
inline void* heap_caps_malloc(size_t size, unsigned caps) {
    lastCaps = caps; ++attempts; return memoryFailure ? nullptr : std::malloc(size);
}
inline void* heap_caps_realloc(void* pointer, size_t size, unsigned caps) {
    lastCaps = caps; ++attempts; return memoryFailure ? nullptr : std::realloc(pointer, size);
}
