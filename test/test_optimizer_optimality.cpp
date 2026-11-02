// SPDX-License-Identifier: GPL-2.0-or-later
#include "optimizer_reference.h"
#include <chrono>
#include <cstring>
#include <iostream>
using namespace Hems;
using namespace Hems::Optimizer;
namespace {
constexpr uint32_t Now=1792195200;
struct Fixture {
    Config c; Inputs in; Energy::Series tariff,pv; Plan plan;
    std::vector<OptimizerReference::Slot> slots;
    Fixture(unsigned capacity,unsigned count) {
        c.enabled=c.socBoundsConfigured=true;c.capacityWh=capacity;c.minSoc=10;c.maxSoc=90;
        c.chargePermille=c.dischargePermille=950;c.maxChargeW=c.maxDischargeW=1500;
        c.baseLoadW=500;c.horizonSeconds=count*900;c.terminalPriceConfigured=true;c.wearSource=2;
        c.flow.enabled=true;c.flow.pvAcLimitW=5000;
        in.now=Now;in.storage.device.enabled=in.storage.device.reachable=in.storage.device.hasUpdate=true;
        in.tariffStatus.fresh=in.tariffStatus.planningUsable=true;in.forecastStatus=in.tariffStatus;
        in.storage.socPct=Model::Signal<double>::fresh(15);
        slots.resize(count);
    }
    void run() {
        tariff.count=pv.count=slots.size();uint32_t cursor=Now;
        for(unsigned t=0;t<slots.size();++t) {
            auto const&s=slots[t];tariff.intervals[t]={cursor,cursor+s.seconds,s.price,s.price,true};
            pv.intervals[t]={cursor,cursor+s.seconds,s.pv,0,false};cursor+=s.seconds;
        }
        in.tariff=&tariff;in.forecast=&pv;c.horizonSeconds=cursor-Now;
        bool ok=calculate(c,in,plan);
        if(!ok) std::cerr<<"planner blocked: "<<(plan.blocker?plan.blocker:"unknown")<<" work="<<plan.workUnits<<'\n';
        assert(ok);
    }
    void compare(int targetBoundary=-1) {
        run(); auto reference=OptimizerReference::solve(c,slots,plan.initialWh,targetBoundary);
        int64_t actual=0;uint64_t throughput=0;uint32_t energy=plan.initialWh;
        for(unsigned t=0;t<slots.size();++t) {
            auto const&s=plan.steps[t];int64_t cost;
            assert(s.startWh==energy && OptimizerReference::flow(c,slots[t],s.startWh,s.endWh,cost));
            actual+=cost;throughput+=std::abs(int64_t(s.endWh)-s.startWh);energy=s.endWh;
        }
        actual+=2*(OptimizerReference::terminal(c,slots,energy)-OptimizerReference::terminal(c,slots,plan.initialWh));
        if(actual!=reference.halfNano || throughput!=reference.throughput) {
            std::cerr<<"objective mismatch capacity="<<c.capacityWh<<" actual="<<actual<<" reference="<<reference.halfNano
                <<" throughput="<<throughput<<"/"<<reference.throughput<<'\n';
        }
        assert(actual==reference.halfNano && throughput==reference.throughput);
        assert(plan.optimizedNano==reference.nano && plan.internalAbsWh==reference.throughput);
        assert(plan.workUnits<=MaxWorkUnits);
    }
};
void realisticReferences() {
    uint32_t rng=70439;auto random=[&](){rng=rng*1664525+1013904223;return rng;};
    for(unsigned k=0;k<48;++k) {
        Fixture f(5000*(1+k%4),8);
        f.c.maxChargeW=f.c.maxDischargeW=1000+500*(k%3);
        f.c.chargePermille=f.c.dischargePermille=k%3?950:1000;
        f.c.degradationMicros=k%2?30000:0;f.c.bufferMicros=k%7==0?999:0;
        f.c.flow.maxExportW=k%3?800:0;f.c.flow.pvExport=k%2;
        f.c.flow.batteryExport=k%4==3;f.c.exportConfigured=f.c.flow.pvExport||f.c.flow.batteryExport;f.c.exportMicros=80000;
        f.c.baseLoadW=300+100*(k%4);
        // First half stresses low stock, second half tests wide initial SoC.
        f.in.storage.socPct.value=k<24?10.0+double((random()%600)+1)*100/f.c.capacityWh:10.0+double(random()%8000)/100;
        for(auto&s:f.slots) {
            int32_t prices[]={-100000,30000,60000,150000,300000,600000},powers[]={0,0,300,1000,2500,4000};
            s={900,prices[random()%6],powers[random()%6],f.c.baseLoadW};
        }
        f.compare();
    }
    std::cout<<"48 exhaustive 5/10/15/20 kWh reference comparisons passed\n";
}
void targetsAndEfficiencies() {
    for(unsigned k=0;k<12;++k) {
        Fixture f(5000*(1+k%4),6);
        unsigned charge[]={850,900,930,950,975,999},discharge[]={800,900,950,975,999,1000};
        f.c.chargePermille=charge[k%6];f.c.dischargePermille=discharge[k%6];
        f.c.degradationMicros=30001;f.c.bufferMicros=999;
        f.c.terminalPriceMicros=k%2?80000:0;
        f.c.flow.pvExport=true;f.c.flow.maxExportW=300;f.c.exportConfigured=true;f.c.exportMicros=k%2?-20000:80000;
        f.in.storage.socPct.value=40;
        f.c.policies.targetEnabled=true;f.c.policies.targetSoc=42;f.c.policies.targetDeadline=Now+4*900;
        for(unsigned t=0;t<f.slots.size();++t) f.slots[t]={900,t<2?-50000:t<4?100000:600000,t==1?1200:0,500};
        f.compare(4);
    }
    // Preserve the exact deadline boundary rather than a UTC rounding heuristic.
    Fixture split(10000,3);split.in.storage.socPct.value=40;
    split.slots={{900,400000,0,500},{75,10000,0,500},{825,600000,0,500}};
    split.c.policies.targetEnabled=true;split.c.policies.targetSoc=42;split.c.policies.targetDeadline=Now+975;
    split.c.maxChargeW=10000;split.compare(2);
    Fixture satisfied(10000,2);satisfied.in.storage.socPct.value=80;
    satisfied.c.maxChargeW=satisfied.c.maxDischargeW=100;
    satisfied.c.policies.targetEnabled=true;satisfied.c.policies.targetSoc=30;
    satisfied.c.policies.targetDeadline=Now+900;satisfied.compare(1);
    // Local First's ceiling is not translation invariant. Check both entering
    // the ceiling and leaving/holding it with exactly the current model rules.
    for(bool full:{false,true}) {
        Fixture local(5000,3);local.in.storage.socPct.value=full?90:85;
        local.c.flow.preference=Flow::Preference::LocalFirst;
        local.c.flow.pvExport=true;local.c.flow.batteryExport=true;local.c.flow.maxExportW=2000;
        local.c.exportConfigured=true;local.c.exportMicros=400000;
        local.c.degradationMicros=30000;
        local.slots={{900,200000,4000,500},{900,600000,0,500},{900,-100000,1000,500}};
        local.compare();
    }
    std::cout<<"Targets, deadline split, 85–99.9% efficiencies, negative export prices and Local First saturation passed\n";
}
void accumulatedErrorRegression() {
    // 633b9646's interpolated policy cost EUR 0.004639 more at 24 slots,
    // and EUR 0.013114886 more at 96 slots, than this exhaustive reference.
    for(unsigned count:{24u,96u}) {
        Fixture f(20000,count);f.c.baseLoadW=300;f.c.maxChargeW=f.c.maxDischargeW=2500;
        f.c.degradationMicros=30000;f.in.storage.socPct.value=10;
        for(unsigned t=0;t<count;++t) f.slots[t]={900,t%2?600000:400000,0,300};
        auto begin=std::chrono::steady_clock::now();f.run();
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
        auto reference=OptimizerReference::solve(f.c,f.slots,f.plan.initialWh);
        assert(f.plan.optimizedNano==reference.nano && f.plan.internalAbsWh==reference.throughput);
        std::cout<<"20 kWh / "<<count<<" slots: exact cost="<<f.plan.optimizedNano<<" nano, DP "<<ms
            <<" ms, "<<f.plan.workUnits<<" work units, "<<f.plan.workspaceBytes<<" workspace bytes\n";
    }
}
void runtimeRanges() {
    for(unsigned capacity:{5000u,10000u,20000u}) for(unsigned count:{96u,192u}) {
        Fixture f(capacity,count);f.c.maxChargeW=f.c.maxDischargeW=2500;
        f.c.degradationMicros=30000;f.in.storage.socPct.value=50;
        for(unsigned t=0;t<count;++t) f.slots[t]={900,t%24<8?50000:400000,0,500};
        auto begin=std::chrono::steady_clock::now();f.run();
        auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"Runtime "<<capacity<<" Wh / "<<count<<" slots: "<<ms<<" ms, "<<f.plan.workUnits
            <<" work units, "<<f.plan.workspaceBytes<<" workspace bytes\n";
    }
}
void resourceLimits() {
    Fixture oversized(1000000,96);oversized.c.maxChargeW=oversized.c.maxDischargeW=100000;
    oversized.c.flow.batteryExport=true;for(auto&s:oversized.slots)s={900,-100000,0,500};
    oversized.tariff.count=96;oversized.in.tariff=&oversized.tariff;
    for(unsigned t=0;t<96;++t) oversized.tariff.intervals[t]={Now+t*900,Now+(t+1)*900,-100000,-100000,true};
    assert(!calculate(oversized.c,oversized.in,oversized.plan));
    assert(!std::strcmp(oversized.plan.blocker,"optimizer_state_capacity_exceeded") && oversized.plan.count==0);
    Fixture expensive(20000,96);expensive.c.chargePermille=expensive.c.dischargePermille=999;
    expensive.c.flow.batteryExport=true;expensive.c.flow.maxExportW=3000;
    expensive.tariff.count=96;expensive.in.tariff=&expensive.tariff;
    for(unsigned t=0;t<96;++t) expensive.tariff.intervals[t]={Now+t*900,Now+(t+1)*900,-100000,-100000,true};
    assert(!calculate(expensive.c,expensive.in,expensive.plan));
    assert(!std::strcmp(expensive.plan.blocker,"optimizer_work_limit") && expensive.plan.count==0);
    assert(expensive.plan.workUnits<=MaxWorkUnits);
    std::cout<<"State/work exhaustion fails explicitly without an approximate plan\n";
}
}
int main(int argc,char** argv) {
    realisticReferences();targetsAndEfficiencies();
    if(argc<2 || std::strcmp(argv[1],"--quick")) { accumulatedErrorRegression();runtimeRanges();resourceLimits(); }
}
