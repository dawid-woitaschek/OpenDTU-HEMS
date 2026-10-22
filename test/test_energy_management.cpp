// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Planner.h>
#include <hems/flow/Coordinator.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib>
using namespace Hems;
using namespace Hems::Optimizer;
constexpr uint32_t Now = 1792195200; // UTC midnight, fixed fixture.
struct Fixture {
    Config c; Inputs in; Energy::Series tariff, pv; Plan plan;
    Fixture() {
        c.enabled = c.socBoundsConfigured = true; c.capacityWh = 5000;
        c.minSoc = 10; c.maxSoc = 90; c.chargePermille = c.dischargePermille = 950;
        c.socStepPct=2; c.maxChargeW = c.maxDischargeW = 2500; c.baseLoadW = 500; c.horizonSeconds = 86400;
        c.flow.enabled = true; c.flow.pvAcLimitW = 3000; c.wearSource = 2;
        tariff.count = pv.count = 192;
        for (unsigned n=0; n<192; ++n) {
            tariff.intervals[n] = {Now+n*900, Now+(n+1)*900, 150000, 150000, true};
            pv.intervals[n] = {Now+n*900, Now+(n+1)*900, 0, 0, false};
        }
        in.tariff = &tariff; in.forecast = &pv; in.now = Now;
        in.tariffStatus.fresh = in.tariffStatus.planningUsable = true;
        in.forecastStatus.fresh = in.forecastStatus.planningUsable = true;
        in.storage.device.enabled = in.storage.device.reachable = in.storage.device.hasUpdate = true;
        in.storage.socPct = Model::Signal<double>::fresh(10);
    }
    bool run() { return calculate(c,in,plan); }
    uint32_t peakBefore(unsigned hour) {
        uint32_t peak=plan.initialWh;
        for (unsigned n=0; n<plan.count && plan.steps[n].end<=Now+hour*3600; ++n) peak=std::max(peak,plan.steps[n].endWh);
        return peak;
    }
};
Model::SystemState measurements(double load, double pv, double battery) {
    Model::SystemState s;
    s.grid.powerW = Model::Signal<double>::fresh(load-pv+battery);
    s.pv.powerW = Model::Signal<double>::fresh(pv);
    s.storage.acPowerW = Model::Signal<double>::fresh(battery);
    s.storage.device.reachable = true;
    return s;
}
void coordinator() {
    Flow::Policy p; p.enabled=true; p.pvAcLimitW=3000; p.preference=Flow::Preference::LocalFirst;
    auto decide=[&](Model::SystemState s,int request=0,bool dpl=true,bool headroom=false) {
        return Flow::coordinate(p,s,request,1000,2500,true,true,true,dpl,headroom);
    };
    auto d=decide(measurements(500,1500,1000));
    assert(d.usable && d.batteryW==1000 && d.pvLimitW==1500); // zero net export
    p.maxExportW=800; d=Flow::coordinate(p,measurements(500,1500,0),0,0,2500,false,true,true,true,false);
    assert(d.pvLimitW==1300); // full battery -> 200 W curtailment
    d=decide(measurements(200,0,-1200),-1200);
    assert(d.batteryW==-200 && d.pvLimitW==0); // PV alone cannot fix battery export
    p.maxExportW=0; d=decide(measurements(200,500,-1200),-1200); assert(d.batteryW>=0);
    d=decide(measurements(200,0,0),-1200,true,true); assert(d.batteryW==0); // do not displace curtailed PV
    p.batteryExport=true; p.maxExportW=800;
    d=decide(measurements(200,0,0),-1200); assert(d.batteryW==-1000);
    p.maxExportW=0; d=decide(measurements(200,0,0),-1200); assert(d.batteryW==-200);
    d=decide(measurements(20,0,-200),-1200); assert(d.batteryW==-20); // load drop
    auto s=measurements(200,500,0); s.grid.powerW.quality=Model::Quality::Stale;
    d=decide(s,-1200); assert(!d.usable && d.batteryW==0 && d.pvLimitW==0);
    d=decide(measurements(200,0,-1200),-1200,false); assert(!d.usable && d.batteryW==0);
    p.preference=Flow::Preference::ExportFirst; p.maxExportW=800;
    d=Flow::coordinate(p,measurements(500,1500,0),1000,1000,2000,true,true,false,true,false);
    assert(d.batteryW==200);
    Flow::Mailbox bus; bus.publish({1000,1300,true}); assert(bus.cap(3000,1001)==1300 && bus.cap(3000,5001)==0);
    bus.publish({5000,0,false}); assert(bus.cap(3000,9000)==3000);
}
void economics() {
    Fixture f; f.c.horizonSeconds=7200; f.c.degradationMicros=30000; f.c.terminalPriceConfigured=true;
    for (unsigned n=4;n<8;++n) f.tariff.intervals[n].value=f.tariff.intervals[n].importPrice=180000;
    assert(f.run()); assert(f.plan.internalAbsWh==0); // 95/95, 15 -> 18, 3 ct wear: closed-cycle uneconomic
    for (unsigned n=4;n<8;++n) f.tariff.intervals[n].value=f.tariff.intervals[n].importPrice=350000;
    assert(f.run() && f.plan.internalAbsWh>0 && f.plan.optimizedNano<f.plan.baselineNano);
    assert(f.plan.degradationNano==int64_t(f.plan.internalAbsWh)*30000/2);
    auto cost=f.plan.optimizedNano; f.c.bufferMicros=10000; assert(f.run() && f.plan.optimizedNano>=cost);
    f.c.bufferMicros=0; f.c.exportConfigured=true; f.c.exportMicros=900000; f.c.baseLoadW=0;
    assert(f.run()); for(unsigned n=0;n<f.plan.count;++n) assert(f.plan.steps[n].batteryW>=0 && f.plan.steps[n].gridExportWs==0);
    f.c.flow.batteryExport=true; f.c.flow.maxExportW=1000;
    assert(f.run() && f.plan.exportNano>0);
    // PV opportunity cost remains separate: valuable exports can dominate later use.
    Fixture opportunity; opportunity.c.horizonSeconds=7200; opportunity.c.exportConfigured=true;
    opportunity.c.exportMicros=400000; opportunity.c.flow.maxExportW=3000;
    opportunity.c.terminalPriceConfigured=true;
    for(unsigned n=0;n<4;++n) opportunity.pv.intervals[n].value=2000;
    for(unsigned n=4;n<8;++n) opportunity.tariff.intervals[n].value=opportunity.tariff.intervals[n].importPrice=350000;
    assert(opportunity.run() && opportunity.peakBefore(1)==500);
    opportunity.c.flow.preference=Flow::Preference::LocalFirst;
    assert(opportunity.run() && opportunity.peakBefore(1)>500); // configurable priority overrides pure economics
    for (auto& price:opportunity.tariff.intervals) price.value=price.importPrice=-100000;
    opportunity.c.exportConfigured=false; opportunity.c.flow.preference=Flow::Preference::Economic;
    assert(opportunity.run() && opportunity.plan.optimizedNano<0);
}
void planningEnvelopes() {
    Fixture f; f.c.horizonSeconds=900; f.in.storage.socPct.value=90;
    f.c.policies.dischargeAllowed=false; f.pv.intervals[0].value=1500; f.c.flow.maxExportW=800;
    assert(f.run() && f.plan.steps[0].gridExportWs==800*900 && f.plan.curtailedWs==200*900);
    f.in.observedPvAcLimitW=1300;
    assert(f.run() && f.plan.steps[0].pvW==1300 && f.plan.curtailedWs==0);
    f.c.flow.maxExportW=0; assert(f.run() && f.plan.steps[0].gridExportWs==0 && f.plan.curtailedWs==800*900);
    f.in.flowTopologySupported=false; assert(!f.run() && !strcmp(f.plan.blocker,"coordination_topology_unsupported"));
    Fixture bounded; bounded.in.storageLimits={true,400,300}; bounded.c.horizonSeconds=7200;
    for(unsigned n=4;n<8;++n) bounded.tariff.intervals[n].value=bounded.tariff.intervals[n].importPrice=400000;
    assert(bounded.run()); for(unsigned n=0;n<bounded.plan.count;++n) assert(bounded.plan.steps[n].batteryW<=400 && bounded.plan.steps[n].batteryW>=-300);
    // Reliable supplied future EV restrictions constrain later slots too.
    Fixture ev; ev.c.horizonSeconds=7200; ev.in.storage.socPct.value=80;
    for(unsigned n=4;n<8;++n) ev.tariff.intervals[n].value=ev.tariff.intervals[n].importPrice=400000;
    Energy::Series restrictions; restrictions.count=1; restrictions.intervals[0]={Now+3600,Now+7200,1,0,false};
    ev.in.evRestrictions=&restrictions; assert(ev.run());
    for(unsigned n=4;n<8;++n) assert(ev.plan.steps[n].batteryW>=0);
    // Unknown future prices use an explicit, SoC-independent fallback assumption.
    Fixture tail; tail.c.horizonSeconds=7200; tail.tariff.count=8;
    for(unsigned n=4;n<8;++n) tail.tariff.intervals[n].value=tail.tariff.intervals[n].importPrice=400000;
    assert(tail.run() && !tail.plan.terminalFutureCovered && tail.plan.terminalPrice==150000);
    for(unsigned soc: {20u,40u,80u}) { tail.in.storage.socPct.value=soc; assert(tail.run() && tail.plan.terminalPrice==150000); }
    tail.tariff.count=192; assert(tail.run() && tail.plan.terminalFutureCovered);
    // Odd Wh and odd micro coefficients retain half-nano costs until plan totals.
    Fixture tiny; tiny.c.capacityWh=50; tiny.c.socStepPct=2; tiny.c.baseLoadW=20;
    tiny.c.horizonSeconds=7200; tiny.c.chargePermille=tiny.c.dischargePermille=1000;
    tiny.c.degradationMicros=1; tiny.c.terminalPriceConfigured=true;
    for(unsigned n=4;n<8;++n) tiny.tariff.intervals[n].value=tiny.tariff.intervals[n].importPrice=500000;
    assert(tiny.run() && tiny.plan.internalAbsWh>0 && tiny.plan.degradationNano==static_cast<int64_t>(tiny.plan.internalAbsWh)/2);
}
void horizons() {
    Fixture f; f.c.capacityWh=3000;
    // Required expensive-morning load determines quantity, not a fixed 60%.
    for(unsigned n=24;n<40;++n) f.tariff.intervals[n].value=f.tariff.intervals[n].importPrice=400000;
    assert(f.run()); auto larger=f.peakBefore(6); assert(larger>500);
    f.c.baseLoadW=250; assert(f.run()); auto smaller=f.peakBefore(6); assert(smaller<larger);
    // Upcoming midday PV before an expensive evening reduces night stock.
    Fixture solar; solar.c.baseLoadW=250; solar.c.capacityWh=3000; for(unsigned n=0;n<24;++n) solar.tariff.intervals[n].value=solar.tariff.intervals[n].importPrice=50000;
    for(unsigned n=64;n<80;++n) solar.tariff.intervals[n].value=solar.tariff.intervals[n].importPrice=400000;
    assert(solar.run()); auto noPv=solar.peakBefore(6);
    for(unsigned n=44;n<60;++n) solar.pv.intervals[n].value=2000;
    assert(solar.run() && solar.peakBefore(6)<noPv);
    // Rising start SoC never creates a rising end floor or free consumption.
    f.c.baseLoadW=500;
    for(unsigned soc: {20u,40u,60u,80u}) {
        f.in.storage.socPct.value=soc; assert(f.run());
        assert(f.plan.finalWh<f.plan.initialWh && f.plan.terminalNano>0);
        auto old=f.plan; f.c.hysteresisMicros=1000000;
        Plan retained; assert(evaluateRetained(f.c,f.in,f.plan,old,retained) && retained.retainedByHysteresis);
        old.steps[0].endWh=old.steps[0].startWh-100;
        f.in.currentConstraints.dischargeAllowed=false;
        assert(!evaluateRetained(f.c,f.in,f.plan,old,retained));
        assert(f.run() && f.plan.steps[0].batteryW>=0); f.in.currentConstraints.dischargeAllowed=true;
    }
    // Flat prices do not create an artificial terminal fill incentive.
    Fixture flat; flat.in.storage.socPct.value=50; assert(flat.run() && flat.plan.internalAbsWh==0);
    // Replan through a cheap charging interval, then expensive load.
    Fixture repeated; repeated.c.horizonSeconds=14400;
    for(unsigned n=8;n<24;++n) repeated.tariff.intervals[n].value=repeated.tariff.intervals[n].importPrice=400000;
    for(unsigned n=0;n<12;++n) {
        assert(repeated.run()); auto const first=repeated.plan.steps[0];
        repeated.in.storage.socPct.value=100.0*first.endWh/repeated.c.capacityWh;
        repeated.in.now=first.end;
        assert(repeated.plan.finalWh<=repeated.c.capacityWh*repeated.c.maxSoc/100);
    }
    std::cout<<"Charge stock (Wh): load500="<<larger<<", load250="<<smaller<<", noPV="<<noPv<<", withPV="<<solar.peakBefore(6)<<'\n';
}
void loadLearning() {
    setenv("TZ","UTC",1); tzset();
    Load::Profile profile;
    auto state=measurements(600,1000,-200); // import -600; household remains 600
    uint32_t ms=0;
    for(unsigned day=0;day<3;++day) {
        auto utc=Now+day*86400;
        for(unsigned t=0;t<=900;t+=10) { ms+=10000; profile.observe(state,utc+t,ms); }
    }
    auto e=profile.estimate(Now+3*86400,200); assert(e.learned && e.watts==600 && e.observations==3);
    assert(!profile.estimate(Now+11*86400,200).learned);
    Load::Profile ev;
    state.evse.device.enabled=true; state.evse.charging=Model::Signal<bool>::fresh(true);
    state.evse.allowedCurrentA=Model::Signal<double>::fresh(16); // never converted to measured power
    for(unsigned t=0;t<=900;t+=10) ev.observe(state,Now+t,t*1000);
    assert(!ev.dirty);
    state.evse.powerW=Model::Signal<double>::fresh(300); // reliable separate meter only
    for(unsigned day=0;day<3;++day) for(unsigned t=0;t<=900;t+=10) ev.observe(state,Now+day*86400+t,day*86400000+t*1000);
    e=ev.estimate(Now+3*86400,200); assert(e.learned && e.watts==300);
    // Missing measurements are unknown; replayed clock time cannot accumulate coverage.
    Load::Profile gaps; state=measurements(500,0,0);
    for(unsigned t=0;t<600;t+=10) gaps.observe(state,Now+t,t*1000);
    state.grid.powerW.quality=Model::Quality::Stale;
    for(unsigned t=600;t<=900;t+=10) gaps.observe(state,Now+t,t*1000);
    assert(!gaps.dirty);
    auto hash=Load::checksum(profile.history); auto restored=profile;
    assert(hash==Load::checksum(restored.history)); restored.history.days[0].watts[0]++; assert(hash!=Load::checksum(restored.history));
    // DST folds and spring gaps use UTC durations, never fictitious zero bins.
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
    for(uint32_t day: {1774742400u,1792886400u}) {
        Load::Profile dst; state=measurements(500,0,0);
        for(unsigned t=0;t<=4*3600;t+=10) dst.observe(state,day+t,t*1000);
        unsigned bins=0; for(auto const& d:dst.history.days) for(auto coverage:d.coverage) bins+=coverage>0;
        assert(bins>=12 && bins<=16);
    }
    setenv("TZ","UTC",1); tzset();
    std::cout<<"History="<<sizeof(Load::History)<<", learner="<<sizeof(Load::Profile)<<", forecast="<<MaxSteps*8<<", Plan="<<sizeof(Plan)<<", Config="<<sizeof(Config)<<" native bytes (not ESP32 performance)\n";
}
struct Simulated { double bill = 0, wear = 0, terminal = 0; double total() const { return bill + wear + terminal; } };
Simulated simulate(Config const& c, Plan const& plan, bool predictable = true) {
    double energy = plan.initialWh; Simulated result;
    auto inventory = [&](double wh) { return std::min<double>(std::max(0.0, wh - c.capacityWh*c.minSoc/100), plan.terminalDemandWh) * c.dischargePermille / 1000 * plan.terminalPrice; };
    for(unsigned n=0;n<plan.count;++n) {
        auto const& step=plan.steps[n]; unsigned hour=(step.start-Now)/3600;
        double load=predictable ? (hour>=6 && hour<10 ? 304 : 152) : 200;
        double hours=double(step.end-step.start)/3600;
        double delta=double(step.endWh)-step.startWh;
        if(delta<0) delta=-std::min({-delta,load*hours*1000/c.dischargePermille,energy-c.capacityWh*c.minSoc/100});
        else delta=std::min(delta,double(c.capacityWh*c.maxSoc/100)-energy);
        double ac=delta>0 ? delta*1000/c.chargePermille : delta*c.dischargePermille/1000;
        result.bill+=(load*hours+ac)*step.usedPrice;
        result.wear+=std::abs(delta)*(c.degradationMicros+c.bufferMicros)/2;
        energy+=delta;
    }
    result.terminal=inventory(plan.initialWh)-inventory(energy);
    return result;
}
void forecastAndReferenceComparison() {
    setenv("TZ","UTC",1); tzset();
    Fixture f; f.c.capacityWh=4000; f.c.baseLoadW=200; f.c.degradationMicros=30000;
    f.c.wearSource=2;
    for(unsigned n=24;n<40;++n) f.tariff.intervals[n].value=f.tariff.intervals[n].importPrice=400000;
    Load::Profile profile; uint32_t ms=0;
    for(unsigned day=3;day>0;--day) for(unsigned t=0;t<=86400;t+=10) {
        unsigned hour=(t/3600)%24;
        auto measured=measurements(hour>=6 && hour<10 ? 304 : 152,0,0);
        profile.observe(measured,Now-day*86400+t,ms); ms+=10000;
    }
    assert(f.run()); auto fixed=f.plan;
    f.c.automaticLoad=true; f.in.loadProfile=&profile; assert(f.run()); auto learned=f.plan;
    double learnedError=0, fixedError=0, changedError=0;
    for(unsigned n=0;n<96;++n) {
        auto predicted=profile.estimate(Now+n*900,200);
        unsigned hour=n/4, actual=hour>=6 && hour<10 ? 304 : 152;
        learnedError+=std::abs(double(predicted.watts)-actual); fixedError+=std::abs(200.0-actual);
        changedError+=std::abs(double(predicted.watts)-200);
    }
    assert(learnedError<fixedError && changedError>0); // Three days are not a guarantee when the pattern changes.
    Plan warp=learned; uint32_t energy=warp.initialWh;
    for(unsigned n=0;n<warp.count;++n) {
        auto& slot=warp.steps[n]; slot.startWh=energy;
        unsigned quarter=(slot.start-Now)/900;
        // Existing installation's EXAMPLE workaround, solely a benchmark.
        if(quarter>=5 && quarter<13) energy=std::min<uint32_t>(2400,energy+250);
        if(quarter>=24 && quarter<40) energy-=std::min<uint32_t>(80,energy-400);
        slot.endWh=energy;
    }
    // Simulations share initial stock, fresh actual loads, prices, efficiencies,
    // permissions, wear and continuation inventory valuation.
    auto learnedResult=simulate(f.c,learned), fixedResult=simulate(f.c,fixed), warpResult=simulate(f.c,warp);
    assert(learnedResult.total()<fixedResult.total() && learnedResult.total()<warpResult.total());
    std::cout<<"Predictable-day load MAE W: profile="<<learnedError/96<<", constant="<<fixedError/96
        <<"; changed-day profile="<<changedError/96<<", constant=0\n";
    std::cout<<"Same-state benchmark EUR (bill / wear / inventory / objective): profile="<<learnedResult.bill/1e9<<" / "<<learnedResult.wear/1e9<<" / "<<learnedResult.terminal/1e9<<" / "<<learnedResult.total()/1e9
        <<"; constant="<<fixedResult.bill/1e9<<" / "<<fixedResult.wear/1e9<<" / "<<fixedResult.terminal/1e9<<" / "<<fixedResult.total()/1e9
        <<"; WARP="<<warpResult.bill/1e9<<" / "<<warpResult.wear/1e9<<" / "<<warpResult.terminal/1e9<<" / "<<warpResult.total()/1e9<<'\n';
}
void config() {
    Fixture f; JsonDocument doc; serializeConfig(f.c,doc.to<JsonObject>()); Config decoded;
    assert(parseConfig(doc.as<JsonVariantConst>(),decoded)); assert(decoded.flow.enabled && !decoded.flow.batteryExport);
    doc.remove("energy_management"); doc.remove("dispatch_enabled"); assert(parseConfig(doc.as<JsonVariantConst>(),decoded));
    assert(!decoded.flow.enabled && !decoded.dispatchEnabled && !decoded.automaticLoad && !decoded.flow.batteryExport);
    serializeConfig(f.c,doc.to<JsonObject>()); doc["energy_management"]["battery_export"]="true"; assert(!parseConfig(doc.as<JsonVariantConst>(),decoded));
    serializeConfig(f.c,doc.to<JsonObject>()); doc["energy_management"]["wear_source"]=0; doc["energy_management"]["degradation_micros"]=30000; assert(!parseConfig(doc.as<JsonVariantConst>(),decoded));
    serializeConfig(f.c,doc.to<JsonObject>()); assert(measureJson(doc)<=2048);
}
int main() { coordinator(); economics(); planningEnvelopes(); horizons(); loadLearning(); forecastAndReferenceComparison(); config(); std::cout<<"Energy management deterministic scenarios passed\n"; }
