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
        c.terminalPriceConfigured=true; c.terminalPriceMicros=200000;
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
        c.terminalPriceMicros=std::max(0,std::min(cheap,expensive));
        for (size_t i=0;i<tariff.count;++i) { tariff.intervals[i].value=tariff.intervals[i].importPrice=i<8 ? cheap : expensive; }
    }
    bool moves() const { for(size_t i=0;i<p.count;++i) if(p.steps[i].startWh!=p.steps[i].endWh) return true; return false; }
    void bounds() const {
        assert(p.finalWh>=c.capacityWh*c.minSoc/100 && p.optimizedNano<=p.baselineNano);
        for(size_t i=0;i<p.count;++i) {
            auto const& s=p.steps[i]; assert(s.endWh>=400 && s.endWh<=1800);
            assert(s.batteryW<=int32_t(c.maxChargeW) && -s.batteryW<=int32_t(c.maxDischargeW));
            if(i) assert(s.startWh==p.steps[i-1].endWh);
            assert(s.gridImportWs>=0 && s.gridExportWs>=0);
        }
    }
};
void policyTests()
{
    Fixture f; f.spread(); assert(f.run() && f.moves());
    f.c.policies.gridChargingAllowed = false;
    assert(f.run());
    for (size_t i=0;i<f.p.count;++i) assert(f.p.steps[i].batteryW<=0); // Existing stock has a priced continuation value; no grid refill.
    f.in.forecastStatus.planningUsable = f.in.forecastStatus.fresh = true;
    for (size_t i=0; i<8; ++i) f.forecast.intervals[i].value=3000;
    assert(f.run() && f.moves() && f.p.pvIncluded);
    for (size_t i=0; i<f.p.count; ++i) {
        auto const& slot=f.p.steps[i];
        if(slot.batteryW>0) assert(slot.gridImportWs==0); // PV must cover load before battery.
    }
    f=Fixture(); f.spread(); f.c.policies.dischargeAllowed=false;
    assert(f.run() && !f.moves());
    f=Fixture(); f.spread(); f.c.minSoc=40; f.c.maxSoc=60;
    assert(f.run());
    for (size_t i=0; i<f.p.count; ++i) assert(f.p.steps[i].endWh>=800 && f.p.steps[i].endWh<=1200);

    // A hard deadline makes the DP choose the least-cost reachable slots, including an exact non-grid target state.
    f=Fixture(); f.c.policies.targetEnabled=true; f.c.policies.targetSoc=73;
    f.c.terminalPriceMicros=50000; f.c.policies.targetDeadline=Now+1875; f.c.policies.dischargeAllowed=false;
    f.c.chargePermille=f.c.dischargePermille=1000;
    f.tariff.intervals[1].value=f.tariff.intervals[1].importPrice=50000;
    assert(f.run() && !strcmp(f.p.targetState,"target_planned"));
    assert(f.p.steps[0].startWh==f.p.steps[0].endWh);
    assert(f.p.steps[1].endWh==1460); // All required charge in cheaper interval, not selected outside planner.
    bool deadline=false;
    for (size_t i=0; i<f.p.count; ++i) {
        if(f.p.steps[i].end==Now+1875) { assert(f.p.steps[i].endWh>=1460); deadline=true; }
    }
    assert(deadline && f.p.finalWh>=f.c.capacityWh*f.c.minSoc/100);
    assert(f.p.optimizedNano>=f.p.baselineNano); // Intent may cost money; never invent positive savings.
    JsonDocument report; serializePlan(f.c,f.in,f.p,report.to<JsonObject>());
    assert(report["target_state"]=="target_planned" && report["device_writes"]==false);
    assert(report["modeled_savings_nano"].as<int64_t>()<=0);
    f.c.maxChargeW=100; assert(!f.run() && !strcmp(f.p.blocker,"target_unreachable") && f.p.count==0);
    f.c.maxChargeW=2000; f.c.policies.gridChargingAllowed=false;
    assert(!f.run() && !strcmp(f.p.blocker,"target_unreachable"));
    f.in.forecastStatus.planningUsable=f.in.forecastStatus.fresh=true;
    for(size_t i=0;i<3;++i) f.forecast.intervals[i].value=3000;
    assert(f.run() && f.p.pvIncluded);
    for(size_t i=0;i<f.p.count;++i) if(f.p.steps[i].batteryW>0) assert(f.p.steps[i].gridImportWs==0);
    // Missing, stale and unusable PV never supplies fabricated charging energy.
    f.in.forecastStatus.fresh=false; assert(!f.run());
    f.in.forecastStatus.fresh=true; f.forecast.intervals[1].start+=1; assert(!f.run());
    f.forecast.intervals[1].start-=1; f.in.forecastStatus.planningUsable=false; assert(!f.run());
    f.in.forecastStatus.planningUsable=true;
    f.in.currentConstraints.chargeAllowed=false;
    f.c.policies.targetDeadline=Now+900; assert(!f.run() && !strcmp(f.p.blocker,"target_unreachable"));

    f=Fixture(); f.c.policies.targetEnabled=true; f.c.policies.targetSoc=70; f.c.policies.targetDeadline=Now+7200;
    f.tariff.count=4;
    assert(f.run() && f.p.partial && !strcmp(f.p.targetState,"target_horizon_incomplete"));
    assert(f.p.steps[f.p.count-1].end==Now+3600 && f.p.finalWh>=f.c.capacityWh*f.c.minSoc/100);
    f.tariff.count=192; f.c.horizonSeconds=900;
    assert(f.run() && f.p.partial && !strcmp(f.p.partialReason,"target_horizon_incomplete"));
    f.c.policies.targetDeadline=Now; assert(!f.run() && !strcmp(f.p.blocker,"target_deadline_passed"));
    f.c.policies.targetDeadline=Now+900; f.c.policies.targetSoc=95; assert(!f.run());

    // Exact numeric economics: equal earlier prices can be UI-normal and still fund expensive future import.
    f=Fixture(); f.spread(200000,500000); assert(f.run() && f.moves());
    assert(f.p.steps[7].endWh>f.p.initialWh);
    f.spread(200000,201000); f.c.chargePermille=f.c.dischargePermille=800; assert(f.run());
    for(size_t n=0;n<f.p.count;++n) assert(f.p.steps[n].batteryW<=0); // No uneconomic new closed cycle; priced starting inventory may serve load.
    f=Fixture(); f.spread(500000,50000); f.in.currentConstraints.dischargeAllowed=false;
    assert(f.run() && f.p.steps[0].endWh>=f.p.initialWh); // Protection dominates price.
    f.in.currentConstraints.maxChargePower={true,0}; f.in.currentConstraints.maxDischargePower={true,0};
    assert(f.run() && f.p.steps[0].endWh==f.p.initialWh);

    // The normalized model has no category input. Decorative categories on bridge JSON cannot alter actions.
    f=Fixture(); f.c.effectivePrice=false; f.spread(200000,500000); assert(f.run()); auto numericPlan=f.p;
    JsonDocument classified; classified["schema"]=Energy::schema(Energy::Domain::Tariff);
    classified["source"]="category fixture"; classified["currency"]="EUR"; classified["generated_at"]=Now;
    for(unsigned i=0;i<16;++i) {
        auto row=classified["intervals"].add<JsonObject>(); row["start"]=Now+i*900; row["end"]=Now+(i+1)*900;
        row["price_micros_per_kwh"]=i<8?200000:500000;
    }
    for(auto category : {"cheap","normal","expensive"}) {
        for(auto row:classified["intervals"].as<JsonArray>()) row["category"]=category;
        char const* error; assert(Energy::parseSeries(classified.as<JsonVariantConst>(),Energy::Domain::Tariff,f.tariff,error));
        assert(f.run() && f.p.optimizedNano==numericPlan.optimizedNano && f.p.count==numericPlan.count);
        for(size_t i=0;i<f.p.count;++i) assert(f.p.steps[i].endWh==numericPlan.steps[i].endWh);
    }
    // A negative/cheap price does not mandate charging when the planning ceiling is already reached.
    f=Fixture(); f.c.baseLoadW=0; f.in.storage.socPct=Model::Signal<double>::fresh(90);
    for(auto& slot:f.tariff.intervals) slot.value=slot.importPrice=-100000;
    assert(f.run() && !f.moves());
    // Feasible target plus maximum lattice and a split deadline stay within the fixed allocation.
    f=Fixture(); f.c.minSoc=0; f.c.maxSoc=100; f.c.socStepPct=2; f.c.horizonSeconds=172800;
    f.in.storage.socPct=Model::Signal<double>::fresh(51.1); f.c.policies.targetEnabled=true;
    f.c.policies.targetSoc=73; f.c.policies.targetDeadline=Now+7201;
    assert(f.run() && f.p.states==53 && f.p.count==193 && f.p.finalWh>=f.c.capacityWh*f.c.minSoc/100);

    // Additive persistent/API v1 compatibility: old fields keep their exact meanings.
    f=Fixture();
    JsonDocument config; serializeConfig(f.c,config.to<JsonObject>()); Config decoded;
    config.remove("policies"); assert(parseConfig(config.as<JsonVariantConst>(),decoded));
    assert(decoded.policies.gridChargingAllowed && decoded.policies.dischargeAllowed && !decoded.policies.targetEnabled);
    assert(decoded.minSoc==f.c.minSoc && decoded.maxSoc==f.c.maxSoc);
    decoded.policies.gridChargingAllowed=false; decoded.policies.dischargeAllowed=false;
    decoded.policies.targetEnabled=true; decoded.policies.targetSoc=70; decoded.policies.targetDeadline=Now+7200;
    serializeConfig(decoded,config.to<JsonObject>()); std::string serialized; serializeJson(config,serialized);
    JsonDocument reload; assert(!deserializeJson(reload,serialized)); Config reboot;
    assert(parseConfig(reload.as<JsonVariantConst>(),reboot));
    assert(!reboot.policies.gridChargingAllowed && !reboot.policies.dischargeAllowed && reboot.policies.targetEnabled);
    assert(reboot.policies.targetSoc==70 && reboot.policies.targetDeadline==Now+7200);
    std::cout<<"Policy config bytes="<<serialized.size()<<", config bytes="<<sizeof(Config)<<", max states="<<MaxStates<<'\n';
    config["policies"]["grid_charging_allowed"]="false"; assert(!parseConfig(config.as<JsonVariantConst>(),reboot));
    serializeConfig(decoded,config.to<JsonObject>()); config["policies"]["target_soc_pct"]=99; assert(!parseConfig(config.as<JsonVariantConst>(),reboot));
    serializeConfig(decoded,config.to<JsonObject>()); config["policies"]["target_deadline"]=-1; assert(!parseConfig(config.as<JsonVariantConst>(),reboot));
    config["policies"]=nullptr; assert(!parseConfig(config.as<JsonVariantConst>(),reboot));
    std::cout<<"Storage policies: permissions, bounds, exact deadline, infeasibility, coverage, PV, protection and config compatibility passed\n";
}
int main()
{
    policyTests();
    Fixture f; assert(f.run() && !f.moves() && !f.p.pvIncluded); assert(f.p.baselineNano==f.p.optimizedNano && f.p.baselineNano==800000000);
    f.spread(); assert(f.run() && f.moves() && f.p.optimizedNano<f.p.baselineNano); f.bounds();
    auto first=f.p; assert(f.run()); assert(f.p.optimizedNano==first.optimizedNano);
    for(size_t i=0;i<f.p.count;++i) assert(f.p.steps[i].endWh==first.steps[i].endWh);
    f=Fixture(); f.spread(200000,201000); f.c.chargePermille=f.c.dischargePermille=800;
    assert(f.run()); for(size_t n=0;n<f.p.count;++n) assert(f.p.steps[n].batteryW<=0); // No uneconomic closed cycle.
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
    f.c.exportConfigured=true; f.c.exportMicros=400000; assert(f.run() && !f.moves());
    f.c.flow.batteryExport=true; assert(f.run() && f.moves() && f.p.optimizedNano<0);
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
    f=Fixture(); f.c.chargePermille=f.c.dischargePermille=100; f.spread(); assert(f.run());
    for(size_t n=0;n<f.p.count;++n) assert(f.p.steps[n].batteryW<=0); // No loss-heavy new grid cycle.
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
