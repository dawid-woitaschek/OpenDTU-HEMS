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
    root["external_devices"] = 0;
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

    if (!root["version"].is<uint32_t>() || root["version"].as<uint32_t>() != CONFIG_VERSION_HEMS
            || !root["enabled"].is<bool>() || Configuration.get().Hems.Version != CONFIG_VERSION_HEMS) {
        retMsg["message"] = "A supported HEMS version and boolean enabled setting are required!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    {
        auto guard = Configuration.getWriteGuard();
        auto& config = guard.getConfig();
        config.Hems.Enabled = root["enabled"].as<bool>();
        HemsController.updateSettings();
    }

    WebApi.writeConfig(retMsg);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
