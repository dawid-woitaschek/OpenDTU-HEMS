// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/model/State.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstdlib>
namespace Hems::Load {
constexpr unsigned Days = 7, Bins = 96;
struct Day {
    int32_t civil = 0;
    uint32_t lastUtc = 0;
    uint16_t watts[Bins] {};
    uint8_t coverage[Bins] {};
    uint8_t weekend = 0;
};
struct History { uint32_t version = 1, zone = 0; Day days[Days] {}; };
struct Estimate { uint32_t watts = 0; uint8_t observations = 0, coverage = 0; bool learned = false; };
// Seven local civil days, no raw high-rate history. UTC integration prevents DST
// gaps/folds from creating energy; repeated civil bins are coverage-weighted.
class Profile {
public:
    History history;
    uint32_t revision = 0;
    bool dirty = false;
    void observe(Model::SystemState const& s, uint32_t utc, uint32_t ms) {
        auto zone = timezoneHash();
        if (history.zone != zone) { history = {}; history.zone = zone; ++revision; _valid = false; }
        bool evKnown = !s.evse.device.enabled || s.evse.charging.usable();
        bool charging = s.evse.device.enabled && s.evse.charging.usable() && s.evse.charging.value;
        bool measuredEv = s.evse.powerW.usable() && std::isfinite(s.evse.powerW.value) && s.evse.powerW.value >= 0;
        bool valid = utc >= 1704067200 && s.grid.powerW.usable() && s.pv.powerW.usable()
            && s.storage.acPowerW.usable() && evKnown && (!charging || measuredEv);
        double watts = s.grid.powerW.value + s.pv.powerW.value - s.storage.acPowerW.value
            - (s.evse.device.enabled && measuredEv ? s.evse.powerW.value : 0);
        valid = valid && std::isfinite(watts) && watts >= 0 && watts <= 100000;
        if (_utc && utc > _utc && utc - _utc <= 30 && ms - _ms <= 30000
                && std::abs(static_cast<int64_t>(ms - _ms) - static_cast<int64_t>(utc - _utc) * 1000) <= 2000 && valid && _valid) {
            uint32_t cursor = _utc;
            while (cursor < utc) {
                uint32_t bin = cursor / 900, end = std::min(utc, (bin + 1) * 900);
                if (_bin != bin) { finish(); _bin = bin; }
                _ws += (_watts + watts) * 0.5 * (end - cursor); _seconds += end - cursor;
                cursor = end;
                if (cursor % 900 == 0) { finish(); _bin = cursor / 900; }
            }
        } else if (utc < _utc || (utc > _utc && utc - _utc > 30)) { finish(); _bin = utc / 900; }
        _utc = utc; _ms = ms; _watts = watts; _valid = valid;
    }
    Estimate estimate(uint32_t utc, uint32_t fallback) const {
        Estimate e; e.watts = fallback;
        if (history.zone != timezoneHash()) { return e; }
        time_t timestamp = utc; tm local {}; localtime_r(&timestamp, &local);
        unsigned bin = local.tm_hour * 4 + local.tm_min / 15;
        bool weekend = local.tm_wday == 0 || local.tm_wday == 6;
        unsigned comparable = 0;
        for (auto const& day : history.days) if (usable(day, utc, bin) && day.weekend == weekend) { ++comparable; }
        uint64_t sum = 0, weights = 0, coverage = 0; unsigned n = 0;
        for (auto const& day : history.days) {
            if (!usable(day, utc, bin) || (comparable >= 3 && day.weekend != weekend)) { continue; }
            uint32_t age = (utc - day.lastUtc) / 86400;
            uint32_t weight = (Days + 1 - std::min<unsigned>(Days, age)) * day.coverage[bin] * (day.weekend == weekend ? 2 : 1);
            sum += static_cast<uint64_t>(day.watts[bin]) * weight; weights += weight; coverage += day.coverage[bin]; ++n;
        }
        e.observations = n;
        e.coverage = std::min<unsigned>(100, coverage / 3);
        if (n >= 3 && weights) { e.watts = (sum + weights / 2) / weights; e.learned = true; }
        return e;
    }
private:
    static uint32_t timezoneHash() {
        uint32_t hash = 2166136261u; auto zone = std::getenv("TZ");
        if (zone) { while (*zone) { hash = (hash ^ static_cast<uint8_t>(*zone++)) * 16777619u; } }
        return hash;
    }
    uint32_t _utc = 0, _ms = 0, _bin = 0, _seconds = 0;
    double _watts = 0, _ws = 0;
    bool _valid = false;
    static bool usable(Day const& d, uint32_t utc, unsigned bin) {
        return d.civil && d.lastUtc <= utc && utc - d.lastUtc <= Days * 86400 && d.coverage[bin] >= 80;
    }
    void finish() {
        if (_seconds >= 720 && _bin) {
            time_t timestamp = _bin * 900; tm local {}; localtime_r(&timestamp, &local);
            int32_t civil = (local.tm_year + 1900) * 512 + local.tm_yday;
            Day* day = nullptr;
            for (auto& d : history.days) if (d.civil == civil) { day = &d; break; }
            if (!day) {
                day = &*std::min_element(std::begin(history.days), std::end(history.days), [](Day const& a, Day const& b) { return a.lastUtc < b.lastUtc; });
                *day = {}; day->civil = civil; day->weekend = local.tm_wday == 0 || local.tm_wday == 6;
            }
            unsigned bin = local.tm_hour * 4 + local.tm_min / 15;
            uint32_t coverage = std::min<uint32_t>(100, _seconds / 9), watts = std::min<double>(65535, _ws / _seconds);
            if (day->coverage[bin]) { watts = (static_cast<uint32_t>(day->watts[bin]) * day->coverage[bin] + watts * coverage) / (coverage + day->coverage[bin]); }
            day->watts[bin] = watts; day->coverage[bin] = std::max<unsigned>(coverage, day->coverage[bin]);
            day->lastUtc = _bin * 900 + 900; dirty = true; ++revision;
        }
        _ws = 0; _seconds = 0;
    }
};
inline uint32_t checksum(History const& h) {
    uint32_t hash = 2166136261u;
    auto bytes = reinterpret_cast<uint8_t const*>(&h);
    for (size_t i = 0; i < sizeof(h); ++i) { hash = (hash ^ bytes[i]) * 16777619u; }
    return hash;
}
} // namespace Hems::Load
