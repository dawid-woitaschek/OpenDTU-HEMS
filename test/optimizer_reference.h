// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native-only independent exhaustive oracle. Every integer inventory and every
// power-feasible whole-Wh action is enumerated; no interpolation, optimized
// convolution, production transition helper or production evaluator is used.
#include <hems/optimizer/Planner.h>
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <vector>
namespace OptimizerReference {
using Hems::Optimizer::Config;
struct Slot { uint32_t seconds = 900; int32_t price = 200000, pv = 0; uint32_t load = 500; };
struct Result { int64_t nano = LLONG_MAX/4, halfNano = LLONG_MAX/4; uint64_t throughput = UINT64_MAX; std::vector<uint32_t> energy; };
inline bool flow(Config const& c, Slot const& s, uint32_t from, uint32_t to, int64_t& cost) {
    int64_t delta = int64_t(to)-from, ws = delta >= 0 ? (delta*3600000+c.chargePermille-1)/c.chargePermille
        : -((-delta*3600*c.dischargePermille)/1000);
    int64_t net = (int64_t(s.load)-s.pv)*s.seconds;
    if ((ws > int64_t(c.maxChargeW)*s.seconds) || (-ws > int64_t(c.maxDischargeW)*s.seconds)) return false;
    if (delta < 0 && !c.policies.dischargeAllowed) return false;
    if (delta > 0 && !c.policies.gridChargingAllowed && ws > std::max<int64_t>(0,-net)) return false;
    bool batteryExport = c.flow.enabled ? c.flow.batteryExport : c.exportConfigured;
    if (delta < 0 && !batteryExport && -ws > std::max<int64_t>(0,net)) return false;
    if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::ExportFirst && c.flow.pvExport && delta>0
        && !c.policies.gridChargingAllowed && ws>std::max<int64_t>(0,-net-int64_t(c.flow.maxExportW)*s.seconds)) return false;
    uint32_t ceiling = uint64_t(c.capacityWh)*c.maxSoc/100;
    if (c.flow.enabled && c.flow.preference == Hems::Flow::Preference::LocalFirst && net<0 && c.maxChargeW && from<ceiling) {
        if (delta<0) return false;
        if (to<ceiling && ((delta+1)*3600000+c.chargePermille-1)/c.chargePermille <= std::min(-net,int64_t(c.maxChargeW)*s.seconds)) return false;
    }
    int64_t grid = net+ws;
    if (c.flow.enabled) {
        int64_t permitted = int64_t(c.flow.maxExportW)*s.seconds;
        if (ws < -int64_t(s.load)*s.seconds-permitted) return false;
        // Preserve the current model, including the separate unresolved KD02.
        if (!c.flow.pvExport) permitted = c.flow.batteryExport && delta<0 ? permitted : 0;
        grid += std::min<int64_t>(int64_t(s.pv)*s.seconds,std::max<int64_t>(0,-grid-permitted));
        if (grid < -permitted) return false;
    }
    // Test bounds keep the direct products in range; this intentionally differs
    // from production's quotient/remainder money implementation.
    int64_t import = std::max<int64_t>(0,grid)*s.price/3600;
    int64_t exportRevenue = c.exportConfigured ? std::max<int64_t>(0,-grid)*c.exportMicros/3600 : 0;
    cost = 2*(import-exportRevenue)+std::abs(delta)*(int64_t(c.degradationMicros)+c.bufferMicros);
    return true;
}
inline int64_t terminal(Config const& c, std::vector<Slot> const& slots, uint32_t wh) {
    assert(c.terminalPriceConfigured);
    uint32_t lower=(uint64_t(c.capacityWh)*c.minSoc+99)/100, upper=uint64_t(c.capacityWh)*c.maxSoc/100;
    uint64_t loadWs=0,duration=0;
    for(auto const& s:slots) { loadWs+=uint64_t(s.load)*s.seconds; duration+=s.seconds; }
    uint64_t demand=std::min<uint64_t>(upper-lower,(loadWs/duration)*c.terminalHours*1000/c.dischargePermille);
    int64_t acWs=std::min<uint64_t>(wh-lower,demand)*3600*c.dischargePermille/1000;
    return -acWs*c.terminalPriceMicros/3600;
}
inline Result solve(Config const& c, std::vector<Slot> const& slots, uint32_t initial, int targetBoundary=-1) {
    uint32_t lower=(uint64_t(c.capacityWh)*c.minSoc+99)/100, upper=uint64_t(c.capacityWh)*c.maxSoc/100;
    size_t n=upper-lower+1;
    struct Score { int64_t cost=LLONG_MAX/4; uint64_t throughput=UINT64_MAX; };
    std::vector<Score> previous(n),next(n);
    std::vector<uint32_t> parent(n*slots.size(),UINT32_MAX);
    previous[initial-lower]={0,0};
    uint32_t reachableLo=initial,reachableHi=initial;
    for(size_t t=0;t<slots.size();++t) {
        auto const& s=slots[t]; std::fill(next.begin(),next.end(),Score{});
        for(uint32_t from=reachableLo;from<=reachableHi;++from) if(previous[from-lower].cost!=LLONG_MAX/4) {
            uint32_t charge=uint64_t(c.maxChargeW)*s.seconds*c.chargePermille/3600000;
            uint32_t discharge=((uint64_t(c.maxDischargeW)*s.seconds+1)*1000-1)/(3600*c.dischargePermille);
            auto a=from-std::min(from-lower,discharge), b=from+std::min(upper-from,charge);
            for(uint32_t to=a;to<=b;++to) {
                if(int(t+1)==targetBoundary && to<(uint64_t(c.capacityWh)*c.policies.targetSoc+99)/100) continue;
                int64_t cost; if(!flow(c,s,from,to,cost)) continue;
                auto candidate=Score{previous[from-lower].cost+cost,previous[from-lower].throughput+uint64_t(std::abs(int64_t(to)-from))};
                auto& best=next[to-lower];
                if(candidate.cost<best.cost || (candidate.cost==best.cost && candidate.throughput<best.throughput)) {
                    best=candidate;parent[t*n+to-lower]=from;
                }
            }
        }
        previous.swap(next); reachableLo=lower;reachableHi=upper;
    }
    Result result;uint32_t selected=initial;
    for(uint32_t e=lower;e<=upper;++e) if(previous[e-lower].cost!=LLONG_MAX/4) {
        auto cost=previous[e-lower].cost+2*(terminal(c,slots,e)-terminal(c,slots,initial));
        if(cost<result.halfNano || (cost==result.halfNano && previous[e-lower].throughput<result.throughput)) {
            result.halfNano=cost;result.throughput=previous[e-lower].throughput;selected=e;
            result.nano=previous[e-lower].cost/2+terminal(c,slots,e)-terminal(c,slots,initial);
        }
    }
    if(result.halfNano==LLONG_MAX/4) return result;
    result.energy.resize(slots.size()+1);result.energy.back()=selected;
    for(size_t t=slots.size();t-->0;) result.energy[t]=selected=parent[t*n+selected-lower];
    return result;
}
}
