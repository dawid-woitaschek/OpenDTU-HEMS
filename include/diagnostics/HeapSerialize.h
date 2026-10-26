// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <diagnostics/Heap.h>
#include <ArduinoJson.h>
namespace Diagnostics {
inline void heapContextJson(HeapContext const& c, JsonObject out)
{
    out["component"] = name(c.component); out["operation"] = name(c.operation); out["phase"] = name(c.phase);
    out["sub_index"] = c.subIndex; out["provider"] = name(c.provider); out["plane"] = c.plane; out["checkpoint"] = name(c.point);
}
inline void heapSampleJson(HeapSample const& sample, JsonObject out)
{
    out["uptime_ms"] = sample.uptimeMs;
    out["free_internal_bytes"] = sample.freeInternal; out["minimum_internal_bytes"] = sample.minimumInternal;
    out["largest_internal_bytes"] = sample.largestInternal; out["allocated_internal_bytes"] = sample.allocatedInternal;
    out["free_internal_blocks"] = sample.freeBlocks; out["allocated_internal_blocks"] = sample.allocatedBlocks;
    out["fragmentation_percent"] = fragmentationPercent(sample.freeInternal, sample.largestInternal);
    out["free_psram_bytes"] = sample.freePsram; out["minimum_psram_bytes"] = sample.minimumPsram;
    out["largest_psram_bytes"] = sample.largestPsram; out["allocated_psram_bytes"] = sample.allocatedPsram;
    out["free_psram_blocks"] = sample.freePsramBlocks; out["allocated_psram_blocks"] = sample.allocatedPsramBlocks;
    out["psram_fragmentation_percent"] = fragmentationPercent(sample.freePsram, sample.largestPsram);
    out["stack_free_bytes"] = sample.stackFree;
    heapContextJson(sample.context, out);
}
inline void heapFailureJson(HeapFailure const& failure, JsonObject out)
{
    out["uptime_ms"] = failure.uptimeMs; out["requested_bytes"] = failure.requested;
    out["requested_capabilities"] = failure.capabilities;
    out["function"] = static_cast<char const*>(failure.function); // Copy the local snapshot, never retain its array address.
    out["last_checkpoint_uptime_ms"] = failure.checkpointUptimeMs;
    out["last_checkpoint_age_ms"] = failure.uptimeMs - failure.checkpointUptimeMs;
    out["last_checkpoint_free_internal_bytes"] = failure.checkpointFreeInternal;
    out["last_checkpoint_largest_internal_bytes"] = failure.checkpointLargestInternal;
    out["last_checkpoint_free_psram_bytes"] = failure.checkpointFreePsram;
    out["last_checkpoint_largest_psram_bytes"] = failure.checkpointLargestPsram;
    heapContextJson(failure.context, out);
}
} // namespace Diagnostics
