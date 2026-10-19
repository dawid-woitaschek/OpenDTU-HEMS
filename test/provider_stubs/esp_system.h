// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
inline void esp_fill_random(void* buffer, size_t size) {
    // Deterministic per-call tokens for native protocol tests only.
    static uint8_t next = 1;
    auto* bytes = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < size; ++i) { bytes[i] = next++; }
}
