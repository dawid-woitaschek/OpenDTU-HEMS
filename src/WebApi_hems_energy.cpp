// SPDX-License-Identifier: GPL-2.0-or-later
#include <WebApi_hems.h>
#include <WebApi.h>
#include <hems/energy/Service.h>
#include <hems/profiles/Profile.h>

namespace {
Hems::Energy::Domain domain(AsyncWebServerRequest* request)
{
    return request->url().indexOf("/tariff") >= 0 ? Hems::Energy::Domain::Tariff : Hems::Energy::Domain::Forecast;
}
}
void WebApiHemsClass::initEnergy(AsyncWebServer& server)
{
    for (auto path : { "/api/hems/energy/tariff", "/api/hems/energy/forecast" }) {
        server.on(path, HTTP_GET, [this](AsyncWebServerRequest* request) { onEnergyGet(request); });
        server.on(path, HTTP_POST, [this](AsyncWebServerRequest* request) { onEnergyPost(request); });
    }
    for (auto path : { "/api/hems/energy/tariff/config", "/api/hems/energy/forecast/config",
            "/api/hems/energy/tariff/refresh", "/api/hems/energy/forecast/refresh" }) {
        server.on(path, HTTP_POST, [this](AsyncWebServerRequest* request) { onEnergyPost(request); });
    }
}
void WebApiHemsClass::onEnergyGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }
    auto* response = new AsyncJsonResponse();
    auto output = response->getRoot().to<JsonObject>();
    Hems::Energy::EnergyData.serialize(domain(request), output, request->hasParam("all"));
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
void WebApiHemsClass::onEnergyPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }
    using Hems::Energy::EnergyData;
    using Hems::Energy::MaxJsonMemory;
    using Hems::Energy::MaxPayload;
    using Hems::Energy::Settings;
    using Hems::Energy::parseSettings;
    auto* response = new AsyncJsonResponse();
    auto& output = response->getRoot();
    bool ok = false;
    char const* message = "Invalid settings or normalized interval data. Previous data retained.";
    int code = 400;
    if (request->url().endsWith("/refresh")) {
        ok = EnergyData.requestRefresh(domain(request));
        message = ok ? "Refresh queued." : "Refresh requires an enabled HTTP provider.";
        code = ok ? 202 : 409;
    } else {
        auto limit = request->url().endsWith("/config") ? 1536 : MaxPayload;
        if (!request->hasParam("data", true) || request->getParam("data", true)->value().length() > limit) {
            output["ok"] = false; output["message"] = "Missing or oversized data.";
            response->setCode(413); WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__); return;
        }
        Hems::Profiles::BoundedAllocator allocator(MaxJsonMemory); JsonDocument doc(&allocator);
        auto const& payload = request->getParam("data", true)->value();
        if (!deserializeJson(doc, payload, DeserializationOption::NestingLimit(6))) {
            if (request->url().endsWith("/config")) {
                Settings settings;
                if (parseSettings(doc.as<JsonVariantConst>(), settings)) {
                    ok = EnergyData.configure(domain(request), settings);
                    if (!ok) { code = 500; message = "Failed to persist provider settings."; }
                }
            } else { ok = EnergyData.ingest(domain(request), doc.as<JsonVariantConst>()); }
        }
        if (ok) { message = "Saved."; code = 200; }
    }
    output["ok"] = ok; output["type"] = ok ? "success" : "warning"; output["message"] = message;
    response->setCode(code);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
