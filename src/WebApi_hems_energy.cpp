// SPDX-License-Identifier: GPL-2.0-or-later
#include <WebApi_hems.h>
#include <WebApi.h>
#include <algorithm>
#include <hems/energy/Service.h>
#include <hems/optimizer/Service.h>
#include <hems/profiles/Profile.h>
#include <hems/Controller.h>

namespace {
Hems::Energy::Domain domain(AsyncWebServerRequest* request)
{
    return request->url().indexOf("/tariff") >= 0 ? Hems::Energy::Domain::Tariff : Hems::Energy::Domain::Forecast;
}
}
void WebApiHemsClass::initEnergy(AsyncWebServer& server)
{
    server.on("/api/hems/optimizer", HTTP_GET, [this](AsyncWebServerRequest* r) { onOptimizer(r); });
    for (auto path : { "/api/hems/optimizer/config", "/api/hems/optimizer/recalculate", "/api/hems/optimizer/resume" }) {
        server.on(path, HTTP_POST, [this](AsyncWebServerRequest* r) { onOptimizer(r); });
    }
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
        auto decision = EnergyData.requestRefresh(domain(request)); ok = static_cast<bool>(decision);
        output["reason"] = decision.reason; output["retry_after_seconds"] = decision.retryAfterSeconds;
        message = ok ? "Refresh queued." : decision.reason;
        code = ok ? 202 : 409;
    } else {
        auto limit = request->url().endsWith("/config") ? 8192 : MaxPayload;
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

void WebApiHemsClass::onOptimizer(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }
    using Hems::Optimizer::ShadowOptimizer;
    auto* response = new AsyncJsonResponse(); auto output = response->getRoot().to<JsonObject>();
    if (request->method() == HTTP_GET) {
        ShadowOptimizer.serialize(output, request->hasParam("all"), request->hasParam("offset") ? std::min(256, std::max(0, static_cast<int>(request->getParam("offset")->value().toInt()))) : 0);
        HemsController.serializeEconomic(output["dispatch"].to<JsonObject>());
    }
    else {
        bool ok = false; int code = 400;
        if (request->url().endsWith("/resume")) { ok = ShadowOptimizer.resume(); code = ok ? 200 : 409; }
        else if (request->url().endsWith("/recalculate")) { ok = ShadowOptimizer.requestRecalculate(); code = ok ? 202 : 409; }
        else if (request->hasParam("data", true) && request->getParam("data", true)->value().length() <= 2048) {
            Hems::Profiles::BoundedAllocator allocator(4096); JsonDocument doc(&allocator);
            Hems::Optimizer::Config config;
            if (!deserializeJson(doc, request->getParam("data", true)->value(), DeserializationOption::NestingLimit(3))
                    && Hems::Optimizer::parseConfig(doc.as<JsonVariantConst>(), config)) {
                ok = ShadowOptimizer.configure(config); code = ok ? 200 : 500;
            }
        }
        output["ok"] = ok; output["type"] = ok ? "success" : "warning";
        output["message"] = ok ? "Economic planning settings saved or calculation queued. Dispatch requires explicit opt-in and all live control gates." : "Invalid settings, disabled optimizer, or persistence failure.";
        response->setCode(code);
    }
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
