// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <remote/HemsSnapshot.h>
#include <hems/energy/Remote.h>
#include <hems/optimizer/Planner.h>
// Worst bounded series/schedule, serialized by the same production pure functions.
inline void worstHems(JsonObject out, uint32_t utc) {
    using namespace Hems;
    Model::SystemState state;
    state.grid.powerW = Model::Signal<double>::fresh(-12345.125);
    state.pv.powerW = Model::Signal<double>::fresh(15000.5);
    state.storage.device = {true, true, true, true, 99};
    state.storage.socPct = Model::Signal<double>::fresh(63.5);
    state.storage.acPowerW = Model::Signal<double>::fresh(-2000);
    state.storage.dcPowerW = Model::Signal<double>::stale(-2100);
    state.storage.batteryVoltageV = Model::Signal<double>::fresh(51.2);
    state.storage.batteryCurrentA = Model::Signal<double>::fresh(-41);
    state.storage.temperatureC = Model::Signal<double>::fresh(26.25);
    state.storage.charging = Model::Signal<bool>::fresh(false);
    state.storage.discharging = Model::Signal<bool>::fresh(true);
    state.storage.faultActive = Model::Signal<bool>::fresh(false);
    state.storage.alarmActive = Model::Signal<bool>::fresh(false);
    state.evse.device = {true, true, true, true, 200};
    state.evse.powerW = Model::Signal<double>::fresh(11000);
    state.evse.vehicleConnected = state.evse.charging = Model::Signal<bool>::fresh(true);
    state.evse.allowedCurrentA = Model::Signal<double>::fresh(16);
    Remote::normalizedJson(state, out["normalized"].to<JsonObject>());
    Energy::Settings settings; settings.enabled = true;
    strcpy(settings.url, "http://provider.invalid/feed?token=PRIVATE_PROVIDER_SECRET");
    settings.planes[0].latitudeE6 = 52123456; settings.planes[0].longitudeE6 = 13456789;
    Energy::Series series; strcpy(series.source, "normalized-test"); strcpy(series.currency, "EUR");
    series.count = Energy::MaxIntervals;
    for (unsigned i = 0; i < series.count; ++i) { series.intervals[i] = {utc + i * 900, utc + (i + 1) * 900, 1234567, 1987654, true}; }
    Energy::Status status; status.enabled = status.hasData = status.fresh = status.currentCovered = status.planningUsable = true;
    Energy::remoteJson(Energy::Domain::Tariff, settings, series, status, utc, out["tariff"].to<JsonObject>());
    Energy::remoteJson(Energy::Domain::Forecast, settings, series, status, utc, out["forecast"].to<JsonObject>());
    Optimizer::Config config; config.enabled = config.socBoundsConfigured = config.dispatchEnabled = true;
    config.capacityWh = 5120; config.minSoc = 20; config.maxSoc = 90; config.chargePermille = config.dischargePermille = 950;
    config.maxChargeW = config.maxDischargeW = 2500; config.baseLoadW = 700;
    config.flow.enabled = true; config.wearSource = 1;
    Optimizer::Inputs inputs; inputs.now = utc; inputs.storage = state.storage; inputs.tariffStatus = status; inputs.forecastStatus = status;
    Optimizer::Plan plan; plan.count = Optimizer::MaxSteps; plan.state = "ready";
    for (unsigned i = 0; i < plan.count; ++i) {
        auto& step = plan.steps[i]; step.start = utc + i * 900; step.end = step.start + 900;
        step.startWh = 1234; step.endWh = 2345; step.marketPrice = 1234567; step.importPrice = 1987654;
        step.hasImportPrice = true; step.usedPrice = 1987654; step.pvW = 12345; step.loadW = 1234;
        step.batteryW = 2000; step.gridImportWs = 999999999999; step.gridExportWs = 999999999999;
        step.costNano = -999999999999; step.loadCoverage = 99; step.loadLearned = true;
    }
    auto optimizer = out["optimizer"].to<JsonObject>();
    Optimizer::serializePlan(config, inputs, plan, optimizer, 96);
    optimizer["schedule_total_steps"] = plan.count; optimizer["schedule_serialized_steps"] = optimizer["schedule"].size();
    optimizer["schedule_truncated"] = true;
}
