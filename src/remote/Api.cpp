// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Api.h>
#include <remote/Service.h>
#include <WebApi.h>
#include <AsyncJson.h>
void remoteApiInit(AsyncWebServer& server) {
    server.on("/api/remote-diagnostics/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!WebApi.checkCredentials(request)) { return; }
        auto response = new AsyncJsonResponse();
        response->addHeader("Cache-Control", "no-store");
        Remote::Sender.configuration(response->getRoot().to<JsonObject>());
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
    });
    server.on("/api/remote-diagnostics/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!WebApi.checkCredentials(request)) { return; }
        auto response = new AsyncJsonResponse();
        response->addHeader("Cache-Control", "no-store");
        Remote::Sender.status(response->getRoot().to<JsonObject>());
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
    });
    server.on("/api/remote-diagnostics/config", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!WebApi.checkCredentials(request)) { return; }
        if (request->hasParam("data", true) && request->getParam("data", true)->value().length() > 2048) {
            request->send(413); return;
        }
        auto response = new AsyncJsonResponse(); JsonDocument input;
        if (!WebApi.parseRequestData(request, response, input)) { return; }
        bool ok = Remote::Sender.configure(input.as<JsonVariantConst>());
        response->setCode(ok ? 200 : 400);
        response->getRoot()["success"] = ok;
        // Never return the submitted token, including on validation/storage errors.
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
    });
    server.on("/api/remote-diagnostics/send", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!WebApi.checkCredentials(request)) { return; }
        auto response = new AsyncJsonResponse();
        bool queued = Remote::Sender.requestSend();
        response->setCode(queued ? 202 : 409); response->getRoot()["scheduled"] = queued;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
    });
}
