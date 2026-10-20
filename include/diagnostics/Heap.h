// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <diagnostics/Breadcrumb.h>
#include <cstdint>
#include <cstddef>
namespace Diagnostics {
enum class HeapPoint : uint8_t { Boot, BeforeRequest, Connected, Response, Parsed, Released, WorkerIdle, Observer, BeforeConnect };
enum class HeapProvider : uint8_t { None, Normalized, EnergyCharts, ForecastSolar };
struct HeapContext {
    Component component = Component::None;
    Operation operation = Operation::None;
    Phase phase = Phase::Start;
    int8_t subIndex = -1;
    HeapProvider provider = HeapProvider::None;
    int8_t plane = -1;
    HeapPoint point = HeapPoint::Boot;
    uint8_t reserved = 0;
};
struct HeapSample {
    uint32_t uptimeMs = 0, freeInternal = 0, minimumInternal = 0, largestInternal = 0;
    uint32_t allocatedInternal = 0, freeBlocks = 0, allocatedBlocks = 0, stackFree = 0;
    HeapContext context;
};
struct HeapFailure {
    uint32_t requested = 0, capabilities = 0, uptimeMs = 0;
    // Cached checkpoint values, deliberately NOT described as failure-instant readings.
    uint32_t checkpointUptimeMs = 0, checkpointFreeInternal = 0, checkpointLargestInternal = 0;
    HeapContext context;
    char function[32] {};
};
static_assert(sizeof(HeapSample) == 40 && sizeof(HeapFailure) == 64, "Keep heap diagnostics fixed and small");
template<typename T, size_t Capacity> struct HeapRing {
    struct Cursor { uint32_t sequence; uint8_t count; };
    void push(T const& value) {
        values[next] = value;
        next = (next + 1) % Capacity;
        if (count < Capacity) { ++count; }
        ++total;
    }
    Cursor cursor() const { return { total, count }; }
    bool read(uint32_t sequence, T& value) const {
        auto age = total - sequence;
        if (age >= count) { return false; }
        value = values[(next + Capacity - 1 - age) % Capacity];
        return true;
    }
private:
    T values[Capacity] {};
    uint32_t total = 0;
    uint8_t next = 0, count = 0;
};
struct HeapJournal {
    HeapRing<HeapSample, 16> samples;
    HeapRing<HeapFailure, 4> failures;
    HeapSample boot; // Preserve the first baseline after the rolling history wraps.
    HeapProvider provider = HeapProvider::None;
    int8_t plane = -1;
    void sample(HeapSample const& value) {
        if (value.context.point == HeapPoint::Boot) { boot = value; }
        // An API observer is a different task and must not replace the active
        // worker's provider context while a TLS allocation can still fail.
        if (value.context.point != HeapPoint::Observer) { provider = value.context.provider; plane = value.context.plane; }
        samples.push(value);
    }
    void failed(size_t requested, uint32_t capabilities, char const* function, uint32_t uptimeMs, HeapContext context) {
        HeapFailure failure;
        failure.requested = requested > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(requested);
        failure.capabilities = capabilities; failure.uptimeMs = uptimeMs;
        failure.context = context;
        if (context.component == Component::Energy) { failure.context.provider = provider; failure.context.plane = plane; }
        HeapSample checkpoint;
        auto cursor = samples.cursor();
        if (samples.read(cursor.sequence, checkpoint)) {
            failure.checkpointUptimeMs = checkpoint.uptimeMs;
            failure.checkpointFreeInternal = checkpoint.freeInternal;
            failure.checkpointLargestInternal = checkpoint.largestInternal;
            failure.context.point = checkpoint.context.point;
        }
        if (function) {
            for (size_t i = 0; i + 1 < sizeof(failure.function) && function[i]; ++i) { failure.function[i] = function[i]; }
        }
        failures.push(failure);
    }
};
static_assert(sizeof(HeapJournal) <= 1024, "Heap diagnostics must stay below one KiB");
inline unsigned fragmentationPercent(uint32_t free, uint32_t largest)
{
    return free && largest <= free ? static_cast<uint64_t>(free - largest) * 100 / free : 0;
}
inline char const* name(HeapPoint value) {
    switch (value) {
    case HeapPoint::Boot: return "boot";
    case HeapPoint::BeforeRequest: return "before_request";
    case HeapPoint::Connected: return "connected";
    case HeapPoint::Response: return "response";
    case HeapPoint::Parsed: return "parsed";
    case HeapPoint::Released: return "released";
    case HeapPoint::WorkerIdle: return "worker_idle";
    case HeapPoint::Observer: return "observer";
    case HeapPoint::BeforeConnect: return "before_connect";
    }
    return "unknown";
}
inline char const* name(HeapProvider value) {
    switch (value) {
    case HeapProvider::None: return "none";
    case HeapProvider::Normalized: return "normalized";
    case HeapProvider::EnergyCharts: return "energy_charts";
    case HeapProvider::ForecastSolar: return "forecast_solar";
    }
    return "unknown";
}
} // namespace Diagnostics
