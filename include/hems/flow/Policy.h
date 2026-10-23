// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Hems::Flow {
// Independent of compensation and physical AC device limits. All new control is opt-in.
enum class Preference : uint8_t { Economic, LocalFirst, ExportFirst };
struct Policy {
    bool enabled = false, pvExport = true, batteryExport = false, evProtection = true;
    Preference preference = Preference::Economic;
    uint32_t maxExportW = 0, pvAcLimitW = 0;
};
// Unknown wear still permits shadow analysis; new active coordination requires
// an estimate or explicit coefficient (zero is an intentional acknowledgement).
inline bool economicWearAcknowledged(Policy const& p, uint8_t source) { return !p.enabled || source != 0; }
} // namespace Hems::Flow
