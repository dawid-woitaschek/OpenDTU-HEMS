// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
constexpr unsigned MALLOC_CAP_8BIT = 4, MALLOC_CAP_SPIRAM = 1024, MALLOC_CAP_INTERNAL = 2048;
inline bool testPsram = true, testMemoryFailure = false;
inline size_t internalFree = 128 * 1024;
inline unsigned externalAttempts = 0, internalAttempts = 0;
inline size_t heap_caps_get_total_size(unsigned caps) { return caps & MALLOC_CAP_SPIRAM ? (testPsram ? 8 * 1024 * 1024 : 0) : 256 * 1024; }
inline size_t heap_caps_get_free_size(unsigned) { return internalFree; }
inline size_t heap_caps_get_largest_free_block(unsigned) { return internalFree; }
inline void* heap_caps_malloc(size_t size, unsigned caps) {
    if (caps & MALLOC_CAP_SPIRAM) { ++externalAttempts; if (!testPsram) return nullptr; }
    else { ++internalAttempts; }
    return testMemoryFailure ? nullptr : std::malloc(size);
}
inline void* heap_caps_realloc(void* ptr, size_t size, unsigned caps) {
    if (caps & MALLOC_CAP_SPIRAM) { ++externalAttempts; if (!testPsram) return nullptr; }
    else { ++internalAttempts; }
    return testMemoryFailure ? nullptr : std::realloc(ptr, size);
}
struct multi_heap_info_t {
    size_t total_free_bytes = 120000, minimum_free_bytes = 110000, largest_free_block = 90000;
    size_t total_allocated_bytes = 40000, free_blocks = 3, allocated_blocks = 40;
};
inline void heap_caps_get_info(multi_heap_info_t* out, unsigned) { *out = {}; }
inline void (*allocationFailureHook)(size_t, uint32_t, char const*) = nullptr;
inline int heap_caps_register_failed_alloc_callback(void (*callback)(size_t, uint32_t, char const*)) { allocationFailureHook = callback; return 0; }
