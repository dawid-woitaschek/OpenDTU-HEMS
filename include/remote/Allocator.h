// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstddef>
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif
namespace Remote {
inline void* externalAllocate(size_t size) {
#if defined(ESP32)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
}
class Allocator final : public ArduinoJson::Allocator {
public:
    explicit Allocator(size_t maximum) : _maximum(maximum) {}
    void* allocate(size_t size) override {
        if (size > _maximum || _used > _maximum - size) { return nullptr; }
        auto header = static_cast<Header*>(externalAllocate(sizeof(Header) + size));
        if (!header) { return nullptr; }
        header->size = size; _used += size; return header + 1;
    }
    void deallocate(void* ptr) override {
        if (!ptr) { return; }
        auto header = static_cast<Header*>(ptr) - 1; _used -= header->size; std::free(header);
    }
    void* reallocate(void* ptr, size_t size) override {
        if (!ptr) { return allocate(size); }
        auto header = static_cast<Header*>(ptr) - 1;
        auto old = header->size;
        if (size > _maximum || _used - old > _maximum - size) { return nullptr; }
#if defined(ESP32)
        auto moved = static_cast<Header*>(heap_caps_realloc(header, sizeof(Header) + size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
        auto moved = static_cast<Header*>(std::realloc(header, sizeof(Header) + size));
#endif
        if (!moved) { return nullptr; }
        moved->size = size; _used = _used - old + size; return moved + 1;
    }
private:
    struct alignas(std::max_align_t) Header { size_t size; };
    size_t _used = 0, _maximum;
};
} // namespace Remote
