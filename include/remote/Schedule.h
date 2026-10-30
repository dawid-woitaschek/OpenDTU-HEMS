// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <cstdint>
namespace Remote {
enum Trigger : uint8_t { Periodic = 1, QuarterHour = 2, PlanRevision = 4, Manual = 8, DiagnosticEvent = 16 };
inline void triggersJson(uint8_t flags, JsonArray out) {
    if (flags & Periodic) { out.add("periodic"); }
    if (flags & QuarterHour) { out.add("quarter_hour"); }
    if (flags & PlanRevision) { out.add("plan_revision"); }
    if (flags & Manual) { out.add("manual"); }
    if (flags & DiagnosticEvent) { out.add("diagnostic_event"); }
}
// Scalar-only latest-state coalescer. There is no queue of HEMS snapshots.
struct Schedule {
    uint8_t pending = 0;
    uint32_t periodicMs = 0, slot = 0, revision = 0, coalesced = 0;
    void add(uint8_t flags, bool inFlight) {
        if (pending || inFlight) { ++coalesced; }
        pending |= flags;
    }
    void observe(uint32_t now, uint32_t utc, uint32_t publishedRevision, bool quarter, bool inFlight) {
        if (static_cast<int32_t>(now - periodicMs) >= 0 && !(pending & Periodic)) { add(Periodic, inFlight); }
        if (utc >= 1577836800) {
            auto currentSlot = utc / 900;
            if (slot && slot != currentSlot && quarter) { add(QuarterHour, inFlight); }
            slot = currentSlot;
        }
        if (publishedRevision != revision) { revision = publishedRevision; add(PlanRevision, inFlight); }
    }
    void captured(uint8_t flags, uint32_t now, uint16_t minutes) {
        pending &= ~flags;
        if (flags & Periodic) { periodicMs = now + static_cast<uint32_t>(minutes) * 60000; }
    }
};
} // namespace Remote
