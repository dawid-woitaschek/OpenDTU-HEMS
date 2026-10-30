// SPDX-License-Identifier: GPL-2.0-or-later
#define main existingControlSuite
#include "test_provider_cleanup.cpp"
#undef main
#include <remote/HemsSnapshot.h>
#include <remote/Allocator.h>
#include <hems/energy/Service.h>
#include <hems/optimizer/Service.h>
#include <LittleFS.h>
#include <ctime>
static uint32_t remoteUtc = 1792180800;
extern "C" time_t time(time_t* out) noexcept { if (out) { *out = remoteUtc; } return remoteUtc; }
namespace Hems::Energy {
bool providerGet(char const*, Source, JsonDocument&, NativeInfo&, char const*& error) { error = "test_offline"; return false; }
}
std::string capture() {
    Remote::Allocator allocator(384 * 1024); JsonDocument doc(&allocator);
    assert(Remote::hemsSnapshot(doc.to<JsonObject>()));
    assert(!doc.overflowed()); std::string body; serializeJson(doc, body); return body;
}
std::string economic() { JsonDocument d; HemsController.serializeEconomic(d.to<JsonObject>()); std::string s; serializeJson(d, s); return s; }
int main(int argc, char** argv) {
    assert(argc <= 2);
    using Optimizer::ShadowOptimizer;
    auto profile = load("../device-profiles/storage/Marstek-Venus-E-3.0.json"); selectedProfiles[profile.id] = profile;
    auto& config = Configuration.data.Hems; config.Mode = Mode::Monitor; config.Warp.Enabled = false;
    StorageConfig storage; storage.Enabled = true; storage.Control = ControlMode::MonitorOnly;
    strcpy(storage.Host, "storage.invalid"); strcpy(storage.Profile, profile.id); Storage::StorageSettings.set(storage);
    testNow = 1000; freshRegisters(); registers[30006] = 0;
    Model::testGrid = Model::Signal<double>::fresh(700); Model::testPv = Model::Signal<double>::fresh(0);
    Scheduler scheduler; HemsController.init(scheduler); Energy::EnergyData.init(scheduler); ShadowOptimizer.init();
    bool observeDpl = false;
    auto ticks = [&] { for (unsigned i = 0; i < 8; ++i) { testNow += 1000; if (observeDpl) { Flow::Coordination.observe({testNow, true, false, 2000, true, false}); } scheduler.execute(); runWorker(); } };
    Energy::Settings settings; settings.enabled = true; settings.pricing.enabled = true; settings.pricing.gridMicros = 100000;
    assert(Energy::EnergyData.configure(Energy::Domain::Tariff, settings));
    JsonDocument tariff; tariff["schema"] = Energy::schema(Energy::Domain::Tariff); tariff["source"] = "remote-test";
    tariff["currency"] = "EUR"; tariff["generated_at"] = remoteUtc;
    for (unsigned i = 0; i < 192; ++i) {
        auto row = tariff["intervals"].add<JsonObject>(); row["start"] = remoteUtc + i * 900;
        row["end"] = remoteUtc + (i + 1) * 900; row["price_micros_per_kwh"] = i ? 400000 : 0;
    }
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>()));
    assert(Energy::EnergyData.configure(Energy::Domain::Forecast, settings));
    JsonDocument forecast; forecast["schema"] = Energy::schema(Energy::Domain::Forecast); forecast["source"] = "PV"; forecast["generated_at"] = remoteUtc;
    for (unsigned i = 0; i < 192; ++i) {
        auto row = forecast["intervals"].add<JsonObject>(); row["start"] = remoteUtc + i * 900;
        row["end"] = remoteUtc + (i + 1) * 900; row["power_w"] = 0;
    }
    assert(Energy::EnergyData.ingest(Energy::Domain::Forecast, forecast.as<JsonVariantConst>()));
    Optimizer::Config c; c.enabled = c.socBoundsConfigured = c.dispatchEnabled = true;
    c.capacityWh = 2000; c.minSoc = 20; c.maxSoc = 90; c.chargePermille = c.dischargePermille = 950;
    c.maxChargeW = c.maxDischargeW = 2000; c.baseLoadW = 1000; c.horizonSeconds = 172800;
    c.terminalPriceConfigured = true; c.terminalPriceMicros = 50000;
    assert(ShadowOptimizer.configure(c)); ticks(); assert(ShadowOptimizer.dispatchPlan().valid && writes.empty());
    auto revision = ShadowOptimizer.publishedRevision(); auto before = economic(); auto mailbox = HemsController.getActuationCommandSnapshot(ControlTarget::Storage);
    std::string body;
    for (unsigned i = 0; i < 20; ++i) { body = capture(); }
    assert(writes.empty() && economic() == before && ShadowOptimizer.publishedRevision() == revision);
    assert(HemsController.getActuationCommandSnapshot(ControlTarget::Storage).id == mailbox.id);
    if (argc == 2) { std::ofstream output(argv[1]); output << body; assert(output.good()); }
    JsonDocument out; assert(!deserializeJson(out, body));
    assert(out["global_mode"] == "Monitor" && out["shadow_if_active"]["assumed_mode"] == "Active");
    assert(out["shadow_if_active"]["valid"] == true && out["shadow_if_active"]["action"] == "set_charge_power_w");
    assert(out["shadow_if_active"]["power_w"].as<double>() > 0 && out["control"]["storage"]["permission"] == false);
    assert(out["control"]["storage"]["automatic"]["writes_possible"] == false);
    assert(out["tariff"]["current"]["price_micros_per_kwh"] == 0 && out["tariff"]["current"]["import_price_micros_per_kwh"] == 100000);
    assert(out["tariff"]["available_interval_count"] == 192 && out["tariff"]["serialized_interval_count"] == 96 && out["tariff"]["truncated"] == true);
    assert(out["forecast"]["serialized_interval_count"] == 96 && out["forecast"]["current"]["power_w"] == 0);
    assert(out["optimizer"]["schedule_total_steps"] == 192 && out["optimizer"]["schedule_serialized_steps"] == 96 && out["optimizer"]["schedule_truncated"] == true);
    for (auto key : {"market_price_micros_per_kwh", "import_price_micros_per_kwh", "used_price_micros_per_kwh", "battery_w", "start_wh", "end_wh", "grid_import_wh", "grid_export_wh", "cost_nano", "assumed_load_w", "load_coverage_pct"}) { assert(!out["optimizer"]["schedule"][0][key].isNull()); }
    assert(out["normalized"]["grid"]["power_w"]["quality"] == "fresh" && out["normalized"]["grid"]["power_w"]["value"] == 700);
    Model::testGrid = Model::Signal<double>::stale(700); Model::testPv = {};
    body = capture(); assert(!deserializeJson(out, body));
    assert(out["normalized"]["grid"]["power_w"]["quality"] == "stale" && out["normalized"]["pv"]["ac_power_w"]["value"].isNull());
    assert(out["normalized"]["pv"]["ac_power_w"]["quality"] == "unknown");
    Model::testGrid = Model::Signal<double>::fresh(700); Model::testPv = Model::Signal<double>::fresh(0);
    assert(out["normalized"]["evse"]["charging"]["value"].isNull());
    assert(out["optimizer"]["configuration_generation"].is<uint32_t>() && out["optimizer"]["plan_revision"].is<uint32_t>());
    observeDpl = true; c.flow.enabled = true; c.flow.pvAcLimitW = 2000;
    assert(ShadowOptimizer.configure(c)); ticks();
    auto blockedWear = HemsController.shadowIfActive(); assert(!blockedWear.valid && !strcmp(blockedWear.reason, "waiting_wear_cost"));
    c.wearSource = 1; assert(ShadowOptimizer.configure(c)); ticks();
    assert(HemsController.shadowIfActive().valid && writes.empty());
    // An actual Active context maps to the same command; preview never enqueues it.
    auto preview = HemsController.shadowIfActive();
    config.Mode = Mode::Active; storage.Control = ControlMode::HemsControl;
    Storage::StorageSettings.set(storage); HemsController.updateSettings(); ticks();
    auto actual = HemsController.getActuationCommandSnapshot(ControlTarget::Storage);
    assert(actual.command.action == preview.command.action && actual.command.value == preview.command.value && !writes.empty());
    tariff["source"] = "http://token@feed.invalid/?key=SECRET_SOURCE";
    assert(Energy::EnergyData.ingest(Energy::Domain::Tariff, tariff.as<JsonVariantConst>()));
    ticks(); body = capture(); assert(!deserializeJson(out, body));
    assert(out["tariff"]["source"] == "redacted_source" && out["optimizer"]["tariff_source"] == "redacted_source");
    assert(body.find("SECRET_SOURCE") == std::string::npos);
    // Provider URLs/coordinates are not part of the remote energy serializer.
    settings = {}; settings.enabled = true; settings.source = Energy::Source::ForecastSolar; strcpy(settings.planes[0].name, "PRIVATE_COORDINATE_PLANE"); settings.planes[0].enabled = true; settings.planes[0].peakW = 1000; settings.planes[0].latitudeE6 = 52123456;
    settings.planes[0].longitudeE6 = 13456789; settings.planeCount = 1;
    assert(Energy::EnergyData.configure(Energy::Domain::Forecast, settings));
    settings.source = Energy::Source::Normalized; strcpy(settings.url, "http://provider.invalid/?token=DO_NOT_SEND");
    assert(Energy::EnergyData.configure(Energy::Domain::Tariff, settings));
    body = capture(); assert(body.find("DO_NOT_SEND") == std::string::npos && body.find("provider.invalid") == std::string::npos && body.find("52123456") == std::string::npos);
    std::cout << "rich HEMS cached snapshots/pure Monitor preview/economics/96-step bounds/privacy passed\n";
}
