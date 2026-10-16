// SPDX-License-Identifier: GPL-2.0-or-later
#include <WebApi_profiles.h>
#include <WebApi.h>
#include <hems/Controller.h>
#include <hems/evse/SettingsStore.h>
#include <hems/evse/Telemetry.h>
#include <hems/profiles/FlashStore.h>
#include <memory>
#include <mutex>

namespace {

void sendError(AsyncWebServerRequest* request, int status, char const* message)
{
    auto* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["ok"] = false;
    root["message"] = message;
    response->setCode(status);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

} // namespace

void WebApiProfilesClass::bindingGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }

    auto config = Hems::Evse::BindingSettings.get();
    auto* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["profile"] = static_cast<char const*>(config.Profile);
    root["control_mode"] = Hems::controlModeName(config.Control);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiProfilesClass::bindingPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }

    auto* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) { return; }

    Hems::Evse::BindingConfig candidate;
    if (!Hems::Evse::deserializeBindingConfig(root.as<JsonVariantConst>(), candidate)) {
        response->getRoot()["ok"] = false;
        response->getRoot()["message"] =
            "A valid EVSE profile id and control mode are required";
        response->setCode(400);
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (candidate.Profile[0]) {
        std::unique_lock<std::mutex> lock(Hems::Profiles::runtimeStoreMutex(), std::try_to_lock);
        if (!lock.owns_lock()) {
            sendError(request, 503, "Profile operation busy");
            delete response;
            return;
        }

        auto profile = std::unique_ptr<Hems::Profiles::Profile>(
            new (std::nothrow) Hems::Profiles::Profile());
        if (!profile) {
            sendError(request, 503, "Insufficient memory");
            delete response;
            return;
        }

        bool builtIn = false;
        char const* error = nullptr;
        if (!Hems::Profiles::runtimeStore().get(candidate.Profile, *profile, builtIn, error)) {
            sendError(request, 404, "Selected EVSE profile does not exist");
            delete response;
            return;
        }
        if (!Hems::Evse::supportsProfile(*profile)) {
            sendError(request, 409, "Selected profile is not compatible with EVSE telemetry");
            delete response;
            return;
        }
    }

    if (!Hems::Evse::BindingSettings.set(candidate)) {
        response->getRoot()["ok"] = false;
        response->getRoot()["message"] = "Failed to persist EVSE profile binding";
        response->setCode(500);
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    HemsController.updateSettings();
    auto& result = response->getRoot();
    result["type"] = "success";
    result["ok"] = true;
    result["profile"] = static_cast<char const*>(candidate.Profile);
    result["control_mode"] = Hems::controlModeName(candidate.Control);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
