// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/optimizer/Service.h>
#include <hems/energy/Service.h>
#include <hems/Controller.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <cassert>
#include <ctime>
#include <iostream>
#include <vector>
constexpr uint32_t Now=1792180800;
extern "C" time_t time(time_t* out) noexcept { if(out) *out=Now; return Now; }
struct TestTask { void (*run)(void*); void* context; bool notified=false; };
std::vector<TestTask*> tasks; TestTask* current;
struct Idle {};
int xTaskCreate(void (*run)(void*), char const*, unsigned, void* context, int, TaskHandle_t* task) { *task=new TestTask{run,context}; tasks.push_back(*task); return pdPASS; }
void xTaskNotifyGive(TaskHandle_t task) { task->notified=true; }
uint32_t ulTaskNotifyTake(int,uint32_t) { if(!current->notified) throw Idle{}; current->notified=false; return 1; }
void workers() { for(auto t:tasks) { if(!t->notified) continue; current=t; try{t->run(t->context);}catch(Idle const&){} } }
namespace Hems::Energy {
bool providerGet(char const*,Source,JsonDocument&,NativeInfo&,char const*& error) {error="test_offline";return false;}
}
int main()
{
    using namespace Hems;
    Scheduler scheduler; Energy::EnergyData.init(scheduler); Optimizer::ShadowOptimizer.init();
    scheduler.execute(); workers(); assert(tasks.empty());
    Energy::Settings settings; settings.enabled=true; assert(Energy::EnergyData.configure(Energy::Domain::Tariff,settings));
    JsonDocument wire; wire["schema"]=Energy::schema(Energy::Domain::Tariff); wire["source"]="service fixture"; wire["currency"]="EUR"; wire["generated_at"]=Now-36000;
    for(unsigned i=0;i<16;++i) { auto j=wire["intervals"].add<JsonObject>(); j["start"]=Now+i*900; j["end"]=Now+(i+1)*900; j["price_micros_per_kwh"]=i<8?0:400000; }
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff,wire.as<JsonVariantConst>()));
    auto& storage=optimizerState.storage; storage.device.enabled=storage.device.reachable=storage.device.hasUpdate=true; storage.socPct=Model::Signal<double>::fresh(50);
    Optimizer::Config config; config.enabled=true; config.socBoundsConfigured=true; config.capacityWh=2000; config.minSoc=20; config.maxSoc=90;
    config.chargePermille=config.dischargePermille=950; config.maxChargeW=config.maxDischargeW=2000; config.baseLoadW=1000; config.effectivePrice=false; config.horizonSeconds=14400;
    assert(Optimizer::ShadowOptimizer.configure(config)); scheduler.execute(); workers(); assert(tasks.size()==1);
    JsonDocument result; Optimizer::ShadowOptimizer.serialize(result.to<JsonObject>(),true);
    assert(result["state"]=="ready" && result["shadow_only"]==true && result["device_writes"]==false);
    assert(result["modeled_savings_nano"].as<int64_t>()>0 && result["schedule"].size()==16);
    assert(result["tariff_source"] == "service fixture" && result["currency"] == "EUR");
    storage.socPct=Model::Signal<double>::stale(50); Optimizer::ShadowOptimizer.serialize(result.to<JsonObject>(),true);
    assert(result["state"]=="stale_inputs" && result["schedule"].size()==0);
    assert(Optimizer::ShadowOptimizer.requestRecalculate()); scheduler.execute(); workers();
    Optimizer::ShadowOptimizer.serialize(result.to<JsonObject>(),true); assert(result["state"]=="not_ready");
    assert(result["blockers"][0]=="storage_soc_not_fresh");
    failCommit=true; config.enabled=false; assert(!Optimizer::ShadowOptimizer.configure(config)); failCommit=false;
    Optimizer::ShadowOptimizer.serialize(result.to<JsonObject>(),true); assert(result["config"]["enabled"]==true);
    Optimizer::Service reboot; reboot.init(); reboot.serialize(result.to<JsonObject>(),false); assert(result["config"]["enabled"]==true);
    assert(Optimizer::ShadowOptimizer.configure(config)); assert(!Optimizer::ShadowOptimizer.requestRecalculate());
    for(auto t:tasks) delete t;
    std::cout<<"Production shared-worker optimizer settings, read-only integration, stale-input suppression, recalculation and persistence passed\n";
}
