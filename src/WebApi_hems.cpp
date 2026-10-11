// SPDX-License-Identifier: GPL-2.0-or-later
#include "WebApi_hems.h"
#include "Configuration.h"
#include "WebApi.h"
#include <hems/Controller.h>

void WebApiHemsClass::init(AsyncWebServer& server)
{
    using std::placeholders::_1;
    server.on("/api/hems/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onStatus, this, _1)));
    server.on("/api/hems/config", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onConfigGet, this, _1)));
    server.on("/api/hems/config", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onConfigPost, this, _1)));
}

void WebApiHemsClass::onStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) { return; }

    auto* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["state"] = HemsController.getMode() == Hems::Mode::Monitor ? "Monitor" : "Disabled";
    root["decision"] = "None";
    auto snapshot = HemsController.getWarpSnapshot();
    root["external_devices"] = snapshot.configured ? 1 : 0;
    JsonObject warp = root["warp"].to<JsonObject>();
    warp["configured"] = snapshot.configured;
    warp["enabled"] = snapshot.enabled;
    warp["reachable"] = snapshot.reachable;
    warp["valid"] = snapshot.valid;
    warp["stale"] = snapshot.stale;
    warp["status"] = !snapshot.enabled ? "Disabled" : snapshot.stale ? "Stale"
        : snapshot.valid ? "Connected" : snapshot.reachable ? "Invalid" : "Unreachable";
    warp["error"] = Hems::Modbus::errorName(snapshot.error);
    warp["modbus_exception"] = snapshot.exception;
    warp["successful_polls"] = snapshot.successfulPolls;
    warp["failed_polls"] = snapshot.failedPolls;
    warp["age_ms"] = nullptr;
    warp["last_success_ms"] = nullptr;
    if (snapshot.hasUpdate) {
        warp["age_ms"] = snapshot.ageMs;
        warp["last_success_ms"] = snapshot.lastSuccessMs;
    }
    for (auto key : { "vehicle_connected", "charging", "power_w", "allowed_current_a",
            "session_energy_kwh", "iec_state", "charging_state", "table_version", "meter_available" }) {
        warp[key] = nullptr;
    }
    // No network calls, and no false/zero substitution for invalid/stale data.
    if (snapshot.valid) {
        auto const& sample = snapshot.sample;
        warp["iec_state"] = sample.iecState;
        warp["charging_state"] = sample.chargingState;
        warp["allowed_current_a"] = sample.allowedCurrentA;
        warp["table_version"] = sample.tableVersion;
        warp["meter_available"] = sample.meterAvailable;
        if (sample.vehicleKnown) { warp["vehicle_connected"] = sample.vehicleConnected; }
        if (sample.chargingKnown) { warp["charging"] = sample.charging; }
        if (sample.meterAvailable) {
            warp["power_w"] = sample.powerW;
            warp["session_energy_kwh"] = sample.sessionEnergyKWh;
        }
    }
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiHemsClass::onConfigGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }

    auto* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().as<JsonObject>();
    ConfigurationClass::serializeHemsConfig(Configuration.get().Hems, root);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiHemsClass::onConfigPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }

    auto* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) { return; }
    auto& retMsg = response->getRoot();
    WarpConfig warpConfig;
    bool warpPresent = !root["warp"].isNull();
    bool warpValid = !warpPresent || ConfigurationClass::deserializeWarpConfig(root["warp"], warpConfig);

    if (!root["version"].is<uint32_t>() || root["version"].as<uint32_t>() != CONFIG_VERSION_HEMS
            || !root["enabled"].is<bool>() || Configuration.get().Hems.Version != CONFIG_VERSION_HEMS || !warpValid) {
        retMsg["message"] = "Supported HEMS version, boolean enabled and valid WARP host/port settings are required!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    {
        auto guard = Configuration.getWriteGuard();
        auto& config = guard.getConfig();
        config.Hems.Enabled = root["enabled"].as<bool>();
        // Legacy PR #2 clients may omit WARP; do not erase existing settings.
        if (warpPresent) { config.Hems.Warp = warpConfig; }
        HemsController.updateSettings();
    }

    WebApi.writeConfig(retMsg);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
