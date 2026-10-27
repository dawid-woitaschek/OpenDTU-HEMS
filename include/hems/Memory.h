// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

namespace Hems::Memory {
// Explicit placement for bounded, non-DMA HEMS data. With PSRAM present, do
// not silently consume the INTERNAL network/stack headroom if PSRAM is full.
// No-PSRAM builds retain their existing best-effort allocation behavior.
inline void* allocate(size_t size)
{
#if defined(ESP32)
    auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_8BIT;
    return heap_caps_malloc(size, caps);
#else
    return std::malloc(size);
#endif
}
inline void* reallocate(void* pointer, size_t size)
{
#if defined(ESP32)
    auto caps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_8BIT;
    return heap_caps_realloc(pointer, size, caps);
#else
    return std::realloc(pointer, size);
#endif
}
template<typename T> struct Delete {
    void operator()(T* pointer) const {
#if defined(ESP32)
        if (pointer) { pointer->~T(); std::free(pointer); }
#else
        delete pointer;
#endif
    }
};
template<typename T> using Ptr = std::unique_ptr<T, Delete<T>>;
template<typename T> Ptr<T> make()
{
    static_assert(std::is_nothrow_default_constructible<T>::value, "HEMS data construction must not throw");
#if defined(ESP32)
    auto storage = allocate(sizeof(T));
    return Ptr<T>(storage ? ::new (storage) T() : nullptr);
#else
    return Ptr<T>(new (std::nothrow) T());
#endif
}
} // namespace Hems::Memory
