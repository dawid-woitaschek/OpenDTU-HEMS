// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
namespace Hems::Flow {
// Supported topology: all governed sources are AC solar behind the same meter,
// zero lower floors, no overscaling. Per-device max already includes its rating
// and configured upper limit. DPL's global ceiling is additional, including zero.
inline uint32_t effectivePvCeiling(uint32_t deviceMaxSum, uint32_t totalUpperLimit) {
    return std::min({deviceMaxSum, totalUpperLimit, static_cast<uint32_t>(UINT16_MAX)});
}
// Parser starts at 0 W with default command status OK. Require an actual
// successful command/read timestamp before treating that zero as observed.
inline bool powerLimitKnown(uint16_t watts, uint32_t commandAt, bool commandOk, uint32_t readAt, bool readOk) {
    return watts > 0 || (commandAt && commandOk) || (readAt && readOk);
}
inline uint16_t nonProducingSolarIncrease(uint16_t currentLimit, uint16_t lower, uint16_t maximum) {
    // A known zero limit itself prevents production. Offer capacity to DPL's
    // bounded allocator; this does not authorize an unbounded wake/probe.
    return currentLimit == 0 ? maximum : lower;
}
} // namespace Hems::Flow
