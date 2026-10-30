// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2026 Thomas Basler and others
 */
#include "WebApi_sysstatus.h"
#include <RuntimeDiagnostics.h>
#include <soc/soc_caps.h>
#include "Configuration.h"
#include "NetworkSettings.h"
#include "PinMapping.h"
#include "SerialPortManager.h"
#include "WebApi.h"
#include "__compiled_constants.h"
#include <AsyncJson.h>
#include <CpuTemperature.h>
#include <Hoymiles.h>
#include <LittleFS.h>
#include <ResetReason.h>
#include "RuntimeData.h"
#include <SystemSnapshot.h>

void WebApiSysstatusClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;

    server.on("/api/system/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiSysstatusClass::onSystemStatus, this, _1)));
}

void WebApiSysstatusClass::onSystemStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    // Sample before constructing the diagnostic JSON response. This still
    // includes the inbound API connection; compare identical observation paths.
    if (request->hasParam("heap_history")) { Diagnostics::heapCheckpoint(Diagnostics::HeapPoint::Observer); }
    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();

    root["hostname"] = NetworkSettings.getHostname();

    systemSnapshot(root.as<JsonObject>(), request->hasParam("heap_history"));

    root["cfgsavecount"] = Configuration.get().Cfg.SaveCount;
    root["runtime_savecount"] = RuntimeData.getWriteCountAndTimeString();

    JsonArray uarts = root["uarts"].to<JsonArray>();
    for (auto const& allocation : SerialPortManager.getAllocations()) {
        JsonObject uart = uarts.add<JsonObject>();
        uart["port"] = allocation.first;
        uart["owner"] = allocation.second;
    }

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
