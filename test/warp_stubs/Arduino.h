// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
inline uint32_t millis()
{
    static auto const start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}
inline void delay(unsigned milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
inline size_t strlcpy(char* target, char const* source, size_t capacity)
{
    size_t length = strlen(source);
    if (capacity) {
        size_t copied = length < capacity - 1 ? length : capacity - 1;
        memcpy(target, source, copied);
        target[copied] = 0;
    }
    return length;
}
