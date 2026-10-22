// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/flow/Policy.h>
#include <hems/model/State.h>
#include <algorithm>
#include <cmath>
#include <mutex>
namespace Hems::Flow {
struct Decision {
    int32_t batteryW = 0;
    uint32_t pvLimitW = 0;
    bool usable = false;
    char const* reason = "disabled";
};
// Grid + is import; PV + is AC production; storage + is AC charging.
// Household (including any EV) = grid + PV - storage. DC is never added.
inline Decision coordinate(Policy const& p, Model::SystemState const& s, int32_t requestedW,
    uint32_t chargeLimitW, uint32_t dischargeLimitW, bool chargeAllowed, bool dischargeAllowed,
    bool gridCharging, bool dplAvailable, bool pvHeadroom)
{
    Decision d;
    if (!p.enabled) { d.batteryW = requestedW; return d; }
    d.reason = "measurements_unavailable";
    // Grid charging consumes power and cannot create export. It can remain a
    // safe battery action when PV control/telemetry is unavailable; the joint
    // export guarantee stays explicitly unavailable. Never infer missing PV=0.
    if (gridCharging && chargeAllowed && requestedW > 0 && s.grid.powerW.usable()
            && s.storage.acPowerW.usable() && s.storage.device.reachable && s.storage.device.ageMs <= 10000
            && std::isfinite(s.grid.powerW.value) && std::isfinite(s.storage.acPowerW.value)) {
        d.batteryW = std::min<uint32_t>(requestedW, chargeLimitW);
    }
    if (!s.grid.powerW.usable() || !s.pv.powerW.usable() || !s.storage.acPowerW.usable()
            || !s.storage.device.reachable || s.storage.device.ageMs > 10000 || !std::isfinite(s.grid.powerW.value)
            || !std::isfinite(s.pv.powerW.value) || !std::isfinite(s.storage.acPowerW.value)) { return d; }
    double pv = std::max(0.0, s.pv.powerW.value), battery = s.storage.acPowerW.value;
    double load = s.grid.powerW.value + pv - battery;
    if (load < -50 || load > 1000000) { d.reason = "inconsistent_measurements"; return d; }
    load = std::max(0.0, load);
    double exportLimit = p.maxExportW;
    double discharge = dischargeAllowed ? std::min<double>(dischargeLimitW, std::max(0, -requestedW)) : 0;
    // Never substitute battery energy for usable/curtailed PV. Until a fresh DPL
    // observation rules out PV headroom, battery discharge is conservatively zero.
    if (!p.batteryExport) { discharge = pvHeadroom ? 0 : std::min(discharge, std::max(0.0, load - pv)); }
    discharge = std::min(discharge, load + exportLimit); // PV cannot absorb battery export.
    double charge = chargeAllowed ? std::min<double>(chargeLimitW, std::max(0, requestedW)) : 0;
    double surplus = std::max(0.0, pv - load);
    if (p.preference == Preference::LocalFirst && chargeAllowed) { charge = std::max(charge, std::min<double>(chargeLimitW, surplus)); }
    if (p.preference == Preference::ExportFirst && p.pvExport) { surplus = std::max(0.0, surplus - exportLimit); }
    if (!gridCharging) { charge = std::min(charge, surplus); }
    d.batteryW = static_cast<int32_t>(std::floor(charge)) - static_cast<int32_t>(std::floor(discharge));
    d.pvLimitW = static_cast<uint32_t>(std::max(0.0, std::min<double>(p.pvAcLimitW,
        load + std::min<double>(battery, d.batteryW) + (p.pvExport ? exportLimit : 0))));
    d.usable = dplAvailable;
    d.reason = dplAvailable ? (d.batteryW != requestedW ? "live_flow_restriction" : "coordinated") : "dpl_unavailable";
    if (!dplAvailable) { d.batteryW = gridCharging ? std::max(0, d.batteryW) : 0; d.pvLimitW = 0; }
    return d;
}
struct DplState {
    uint32_t atMs = 0;
    bool available = false, pvHeadroom = true;
    uint32_t physicalAcW = 0;
    bool topologySupported = true;
};
struct Envelope { uint32_t atMs = 0, pvLimitW = 0; bool enabled = false; };
// Small cross-loop mailbox only. No I/O, allocation, device writer or DP in DPL.
class Mailbox {
public:
    void publish(Envelope e) { std::lock_guard<std::mutex> l(_mutex); _envelope = e; }
    Envelope envelope() const { std::lock_guard<std::mutex> l(_mutex); return _envelope; }
    void observe(DplState s) { std::lock_guard<std::mutex> l(_mutex); _dpl = s; }
    DplState dpl() const { std::lock_guard<std::mutex> l(_mutex); return _dpl; }
    uint32_t cap(uint32_t request, uint32_t nowMs) const {
        auto e = envelope(); return !e.enabled ? request : nowMs - e.atMs > 3000 ? 0 : std::min(request, e.pvLimitW);
    }
private:
    mutable std::mutex _mutex;
    Envelope _envelope;
    DplState _dpl;
};
inline Mailbox Coordination;
} // namespace Hems::Flow
