// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Planner.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
using namespace Hems;
using namespace Hems::Optimizer;
constexpr uint32_t Now = 1792180800;
struct Fixture {
    Config c; Inputs in; Energy::Series tariff, forecast; Plan p;
    Fixture() {
        c.socBoundsConfigured = true; c.enabled = true; c.effectivePrice = true; c.horizonSeconds = 14400; c.capacityWh = 2000;
        c.minSoc=20; c.maxSoc=90; c.chargePermille=950; c.dischargePermille=950; c.baseLoadW=1000;
        c.maxChargeW=2000; c.maxDischargeW=2000;
        tariff.count=forecast.count=192; tariff.generatedAt=forecast.generatedAt=Now;
        strcpy(tariff.source,"normalized fixture"); strcpy(tariff.currency,"EUR");
        for (size_t i=0;i<192;++i) {
            tariff.intervals[i] = {Now+uint32_t(i)*900,Now+uint32_t(i+1)*900,200000,200000,true};
            forecast.intervals[i] = {Now+uint32_t(i)*900,Now+uint32_t(i+1)*900,0,0,false};
        }
        in.now=Now; in.tariff=&tariff; in.forecast=&forecast;
        in.tariffStatus.enabled=in.tariffStatus.hasData=in.tariffStatus.fresh=in.tariffStatus.planningUsable=true;
        in.storage.device.enabled=in.storage.device.reachable=in.storage.device.hasUpdate=true;
        in.storage.socPct=Model::Signal<double>::fresh(50);
    }
    bool run() { if (in.tariff) in.tariff=&tariff; if (in.forecast) in.forecast=&forecast; return calculate(c,in,p); }
    void spread(int32_t cheap=50000, int32_t expensive=400000) {
        for (size_t i=0;i<tariff.count;++i) { tariff.intervals[i].value=tariff.intervals[i].importPrice=i<8 ? cheap : expensive; }
    }
    bool moves() const { for(size_t i=0;i<p.count;++i) if(p.steps[i].startWh!=p.steps[i].endWh) return true; return false; }
    void bounds() const {
        assert(p.finalWh>=p.initialWh && p.optimizedNano<=p.baselineNano);
        for(size_t i=0;i<p.count;++i) {
            auto const& s=p.steps[i]; assert(s.endWh>=400 && s.endWh<=1800);
            assert(s.batteryW<=int32_t(c.maxChargeW) && -s.batteryW<=int32_t(c.maxDischargeW));
            if(i) assert(s.startWh==p.steps[i-1].endWh);
            assert(s.gridImportWs>=0 && s.gridExportWs>=0);
        }
    }
};
int main()
{
    Fixture f; assert(f.run() && !f.moves() && !f.p.pvIncluded); assert(f.p.baselineNano==f.p.optimizedNano && f.p.baselineNano==800000000);
    f.spread(); assert(f.run() && f.moves() && f.p.optimizedNano<f.p.baselineNano); f.bounds();
    auto first=f.p; assert(f.run()); assert(f.p.optimizedNano==first.optimizedNano);
    for(size_t i=0;i<f.p.count;++i) assert(f.p.steps[i].endWh==first.steps[i].endWh);
    f=Fixture(); f.spread(200000,201000); f.c.chargePermille=f.c.dischargePermille=800;
    assert(f.run() && !f.moves()); // Losses exceed the price spread.
    f=Fixture(); f.spread(-100000,400000); assert(f.run() && f.moves() && f.p.optimizedNano<f.p.baselineNano); f.bounds();
    f=Fixture(); f.in.forecastStatus.planningUsable=f.in.forecastStatus.fresh=true;
    for(size_t i=0;i<8;++i) f.forecast.intervals[i].value=3000;
    assert(f.run() && f.p.pvIncluded && f.moves() && f.p.optimizedNano<f.p.baselineNano); f.bounds();
    f.in.forecastStatus.fresh=false; assert(f.run() && !f.p.pvIncluded); assert(!f.moves());
    f.in.forecast=nullptr; assert(f.run() && !f.p.pvIncluded);
    f=Fixture(); f.in.tariff=nullptr; assert(!f.run() && !strcmp(f.p.blocker,"tariff_not_usable"));
    f=Fixture(); f.in.tariffStatus.planningUsable=false; assert(!f.run());
    f=Fixture(); f.in.storage.socPct={}; assert(!f.run() && !strcmp(f.p.blocker,"storage_soc_not_fresh"));
    f=Fixture(); f.in.storage.socPct=Model::Signal<double>::stale(50); assert(!f.run());
    f=Fixture(); f.in.storage.device.reachable=false; assert(!f.run());
    f=Fixture(); f.c.capacityWh=0; assert(!f.run() && !strcmp(f.p.blocker,"battery_capacity_missing"));
    f=Fixture(); f.c.chargePermille=0; assert(!f.run() && !strcmp(f.p.blocker,"charge_efficiency_missing"));
    f=Fixture(); f.c.dischargePermille=0; assert(!f.run());
    f=Fixture(); f.in.storage.socPct=Model::Signal<double>::fresh(10); assert(!f.run());
    f=Fixture(); f.c.maxChargeW=100; f.spread(); assert(f.run()); f.bounds();
    f=Fixture(); f.c.maxDischargeW=100; f.spread(); assert(f.run()); f.bounds();
    f=Fixture(); f.c.baseLoadW=0; assert(f.run() && !f.moves() && f.p.optimizedNano==0);
    f.c.exportConfigured=true; f.c.exportMicros=400000; assert(f.run() && f.moves() && f.p.optimizedNano<0);
    f=Fixture(); f.spread(400000,50000); f.in.currentConstraints.dischargeAllowed=false;
    assert(f.run() && f.p.steps[0].endWh>=f.p.steps[0].startWh); f.bounds();
    f=Fixture(); f.spread(); f.in.currentConstraints.chargeAllowed=false;
    assert(f.run() && f.p.steps[0].endWh<=f.p.steps[0].startWh);
    f=Fixture(); f.spread(); f.tariff.intervals[0].hasImportPrice=false;
    assert(!f.run() && !strcmp(f.p.blocker,"price_basis_unavailable")); f.c.effectivePrice=false; assert(f.run());
    f=Fixture(); f.c.horizonSeconds=172800; f.c.socStepPct=2; f.spread();
    auto before=std::chrono::steady_clock::now(); assert(f.run()); auto elapsed=std::chrono::steady_clock::now()-before;
    assert(f.p.count==192 && !f.p.partial && f.p.states<=MaxStates); f.bounds();
    std::cout<<"48h DP: "<<std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()<<" us, states="<<f.p.states<<", plan bytes="<<sizeof(Plan)<<'\n';
    f.in.now+=123; assert(f.run() && f.p.partial); // Last 123 seconds have no tariff; explicit partial plan.
    f=Fixture(); f.tariff.intervals[10].start+=1; assert(f.run() && f.p.partial && f.p.count==10);
    f=Fixture(); f.spread(INT32_MIN,INT32_MAX); assert(f.run()); f.bounds();
    f=Fixture(); f.spread(1,1000000); f.c.chargePermille=f.c.dischargePermille=100; assert(f.run()); f.bounds();
    f=Fixture(); f.spread(0,0); assert(f.run() && !f.moves());
    f=Fixture(); f.c.chargePermille=f.c.dischargePermille=100; f.spread(); assert(f.run() && !f.moves());
    for(unsigned hours : {23,25}) {
        f=Fixture(); f.c.horizonSeconds=hours*3600; f.in.now=Now+9*86400;
        for(size_t i=0;i<f.tariff.count;++i) { f.tariff.intervals[i].start+=9*86400; f.tariff.intervals[i].end+=9*86400; }
        assert(f.run() && f.p.count==hours*4 && !f.moves());
    }
    f=Fixture(); f.c.capacityWh=1000000; f.c.minSoc=0; f.c.maxSoc=100; f.c.socStepPct=2; f.c.horizonSeconds=172800;
    f.c.maxChargeW=f.c.maxDischargeW=f.c.baseLoadW=100000; f.in.storage.socPct=Model::Signal<double>::fresh(51.1);
    f.in.forecastStatus.fresh=f.in.forecastStatus.planningUsable=true;
    f.c.exportConfigured=true; f.c.exportMicros=INT32_MAX;
    for(auto& i:f.forecast.intervals) i.value=6000000;
    for(auto& i:f.tariff.intervals) i.value=i.importPrice=INT32_MAX;
    before=std::chrono::steady_clock::now(); assert(f.run() && f.p.states==52);
    elapsed=std::chrono::steady_clock::now()-before;
    std::cout<<"48h maximum-state/extreme-cost DP: "<<std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()<<" us, states="<<f.p.states<<'\n';
    f=Fixture(); f.c.socBoundsConfigured=false; assert(!f.run() && !strcmp(f.p.blocker,"soc_bounds_not_configured"));
    f=Fixture(); f.c.maxSoc=f.c.minSoc; assert(!f.run());
    f=Fixture(); f.in.now=UINT32_MAX-3600; f.c.horizonSeconds=3600; f.tariff.count=1;
    f.tariff.intervals[0]={f.in.now,UINT32_MAX,200000,200000,true}; assert(f.run() && f.p.steps[f.p.count-1].end==UINT32_MAX);
    f=Fixture(); f.in.now=0; assert(!f.run());
    // End-to-end: actual normalized JSON parsing + freshness providers + normalized telemetry -> production DP.
    JsonDocument wire; wire["schema"]=Energy::schema(Energy::Domain::Tariff); wire["source"]="e2e"; wire["currency"]="EUR"; wire["generated_at"]=Now-36000;
    for(unsigned i=0;i<16;++i) { auto j=wire["intervals"].add<JsonObject>(); j["start"]=Now+i*900; j["end"]=Now+(i+1)*900; j["price_micros_per_kwh"]=i<8?0:400000; }
    f=Fixture(); f.c.effectivePrice=false; char const* error;
    assert(Energy::parseSeries(wire.as<JsonVariantConst>(),Energy::Domain::Tariff,f.tariff,error));
    Energy::JsonProvider provider(Energy::Domain::Tariff); Energy::Settings settings; settings.enabled=true; provider.configure(settings);
    assert(provider.accept(f.tariff,Now,0)); f.in.tariffStatus=provider.status(Now,0);
    wire.clear(); wire["schema"]=Energy::schema(Energy::Domain::Forecast); wire["source"]="e2e PV"; wire["generated_at"]=Now;
    for(unsigned i=0;i<16;++i) { auto j=wire["intervals"].add<JsonObject>(); j["start"]=Now+i*900; j["end"]=Now+(i+1)*900; j["power_w"]=i<8?2000:0; }
    assert(Energy::parseSeries(wire.as<JsonVariantConst>(),Energy::Domain::Forecast,f.forecast,error));
    Energy::JsonProvider pv(Energy::Domain::Forecast); pv.configure(settings); assert(pv.accept(f.forecast,Now,0)); f.in.forecastStatus=pv.status(Now,0);
    assert(f.run() && f.p.pvIncluded && f.p.optimizedNano<f.p.baselineNano); f.bounds();
    JsonDocument result; serializePlan(f.c,f.in,f.p,result.to<JsonObject>());
    assert(result["shadow_only"]==true && result["device_writes"]==false && result["schedule"].size()==16);
    std::cout<<"Economic shadow optimizer constraints, losses, reserves, PV optionality, prices, determinism and end-to-end tests passed\n";
}
