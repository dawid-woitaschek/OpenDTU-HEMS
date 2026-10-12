// SPDX-License-Identifier: GPL-2.0-or-later
#include "WebApi_hems.h"
#include "Configuration.h"
#include "WebApi.h"
#include <hems/Controller.h>
#include <hems/profiles/FlashStore.h>
#include <hems/storage/SettingsStore.h>
#include <hems/storage/Telemetry.h>
#include <memory>
#include <mutex>

namespace {

bool validateStorageProfile(StorageConfig const& config, std::unique_lock<std::mutex>& profileLock,
        char const*& error)
{
    error = nullptr;
    if (!config.Enabled) { return true; }
    if (!config.Profile[0]) {
        error = "A storage profile is required while storage telemetry is enabled";
        return false;
    }

    profileLock = std::unique_lock<std::mutex>(Hems::Profiles::runtimeStoreMutex(), std::try_to_lock);
    if (!profileLock.owns_lock()) {
        error = "Profile operation busy";
        return false;
    }

    auto profile = std::unique_ptr<Hems::Profiles::Profile>(new (std::nothrow) Hems::Profiles::Profile());
    if (!profile) {
        error = "Insufficient memory";
        return false;
    }

    bool builtIn = false;
    char const* storeError = nullptr;
    if (!Hems::Profiles::runtimeStore().get(config.Profile, *profile, builtIn, storeError)) {
        error = "Selected storage profile does not exist";
        return false;
    }
    if (!Hems::Storage::supportsProfile(*profile)) {
        error = "Selected profile is not compatible with storage telemetry";
        return false;
    }
    return true;
}

} // namespace

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

    auto warpSnapshot = HemsController.getWarpSnapshot();
    auto storageSnapshot = HemsController.getStorageSnapshot();
    root["external_devices"] = (warpSnapshot.configured ? 1 : 0) + (storageSnapshot.configured ? 1 : 0);

    JsonObject warp = root["warp"].to<JsonObject>();
    warp["configured"] = warpSnapshot.configured;
    warp["enabled"] = warpSnapshot.enabled;
    warp["reachable"] = warpSnapshot.reachable;
    warp["valid"] = warpSnapshot.valid;
    warp["stale"] = warpSnapshot.stale;
    warp["status"] = !warpSnapshot.enabled ? "Disabled" : warpSnapshot.stale ? "Stale"
        : warpSnapshot.valid ? "Connected" : warpSnapshot.reachable ? "Invalid" : "Unreachable";
    warp["error"] = Hems::Modbus::errorName(warpSnapshot.error);
    warp["modbus_exception"] = warpSnapshot.exception;
    warp["successful_polls"] = warpSnapshot.successfulPolls;
    warp["failed_polls"] = warpSnapshot.failedPolls;
    warp["age_ms"] = nullptr;
    warp["last_success_ms"] = nullptr;
    if (warpSnapshot.hasUpdate) {
        warp["age_ms"] = warpSnapshot.ageMs;
        warp["last_success_ms"] = warpSnapshot.lastSuccessMs;
    }
    for (auto key : { "vehicle_connected", "charging", "power_w", "allowed_current_a",
            "session_energy_kwh", "iec_state", "charging_state", "table_version", "meter_available" }) {
        warp[key] = nullptr;
    }
    // No network calls, and no false/zero substitution for invalid/stale data.
    if (warpSnapshot.valid) {
        auto const& sample = warpSnapshot.sample;
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

    auto storageConfig = Hems::Storage::StorageSettings.get();
    JsonObject storage = root["storage"].to<JsonObject>();
    storage["configured"] = storageSnapshot.configured;
    storage["enabled"] = storageSnapshot.enabled;
    storage["profile_loaded"] = storageSnapshot.profileLoaded;
    storage["reachable"] = storageSnapshot.reachable;
    storage["valid"] = storageSnapshot.valid;
    storage["stale"] = storageSnapshot.stale;
    storage["status"] = !storageSnapshot.configured ? "Disabled" : !storageSnapshot.profileLoaded ? "Invalid"
        : !storageSnapshot.enabled ? "Disabled" : storageSnapshot.stale ? "Stale"
        : storageSnapshot.valid ? "Connected" : storageSnapshot.reachable ? "Invalid" : "Unreachable";
    storage["error"] = Hems::Modbus::errorName(storageSnapshot.error);
    storage["modbus_exception"] = storageSnapshot.exception;
    storage["successful_polls"] = storageSnapshot.successfulPolls;
    storage["failed_polls"] = storageSnapshot.failedPolls;
    storage["profile"] = static_cast<char const*>(storageConfig.Profile);
    storage["host"] = static_cast<char const*>(storageConfig.Host);
    storage["port"] = storageSnapshot.port;
    storage["unit"] = storageSnapshot.unit;
    storage["age_ms"] = nullptr;
    storage["last_success_ms"] = nullptr;
    if (storageSnapshot.hasUpdate) {
        storage["age_ms"] = storageSnapshot.ageMs;
        storage["last_success_ms"] = storageSnapshot.lastSuccessMs;
    }
    for (auto key : { "soc_pct", "ac_power_w", "dc_power_w", "battery_voltage_v", "battery_current_a",
            "temperature_c", "charge_energy_total_kwh", "discharge_energy_total_kwh", "operating_state",
            "fault_code", "alarm_code", "charging", "discharging", "fault_active", "alarm_active" }) {
        storage[key] = nullptr;
    }
    if (storageSnapshot.valid) {
        auto const& sample = storageSnapshot.sample;
        auto number = [&storage](char const* key, Hems::Storage::NumberValue const& value) {
            if (value.valid) { storage[key] = value.value; }
        };
        auto boolean = [&storage](char const* key, Hems::Storage::BoolValue const& value) {
            if (value.valid) { storage[key] = value.value; }
        };
        number("soc_pct", sample.socPct);
        number("ac_power_w", sample.acPowerW);
        number("dc_power_w", sample.dcPowerW);
        number("battery_voltage_v", sample.batteryVoltageV);
        number("battery_current_a", sample.batteryCurrentA);
        number("temperature_c", sample.temperatureC);
        number("charge_energy_total_kwh", sample.chargeEnergyTotalKWh);
        number("discharge_energy_total_kwh", sample.dischargeEnergyTotalKWh);
        number("operating_state", sample.operatingState);
        number("fault_code", sample.faultCode);
        number("alarm_code", sample.alarmCode);
        boolean("charging", sample.charging);
        boolean("discharging", sample.discharging);
        boolean("fault_active", sample.faultActive);
        boolean("alarm_active", sample.alarmActive);
    }

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiHemsClass::onConfigGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }

    auto* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().as<JsonObject>();
    ConfigurationClass::serializeHemsConfig(Configuration.get().Hems, root);
    JsonObject storage = root["storage"].to<JsonObject>();
    serializeStorageConfig(Hems::Storage::StorageSettings.get(), storage);
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
    StorageConfig storageConfig;
    bool enabledPresent = !root["enabled"].isNull();
    bool warpPresent = !root["warp"].isNull();
    bool storagePresent = !root["storage"].isNull();
    bool enabledValid = !enabledPresent || root["enabled"].is<bool>();
    bool warpValid = !warpPresent || ConfigurationClass::deserializeWarpConfig(root["warp"], warpConfig);
    bool storageValid = !storagePresent || deserializeStorageConfig(root["storage"], storageConfig);

    if (!root["version"].is<uint32_t>() || root["version"].as<uint32_t>() != CONFIG_VERSION_HEMS
            || Configuration.get().Hems.Version != CONFIG_VERSION_HEMS || !enabledValid || !warpValid || !storageValid
            || (!enabledPresent && !warpPresent && !storagePresent)) {
        retMsg["message"] = "Supported HEMS version and valid provided settings are required!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    std::unique_lock<std::mutex> profileLock;
    char const* profileError = nullptr;
    if (storagePresent && !validateStorageProfile(storageConfig, profileLock, profileError)) {
        retMsg["message"] = profileError ? profileError : "Invalid storage profile";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (storagePresent && !Hems::Storage::StorageSettings.set(storageConfig)) {
        retMsg["message"] = "Failed to persist storage telemetry settings!";
        retMsg["code"] = WebApiError::GenericWriteFailed;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    if (profileLock.owns_lock()) { profileLock.unlock(); }

    bool mainConfigChanged = enabledPresent || warpPresent;
    if (mainConfigChanged) {
        auto guard = Configuration.getWriteGuard();
        auto& config = guard.getConfig();
        if (enabledPresent) { config.Hems.Enabled = root["enabled"].as<bool>(); }
        if (warpPresent) { config.Hems.Warp = warpConfig; }
    }

    HemsController.updateSettings();
    if (mainConfigChanged) {
        WebApi.writeConfig(retMsg);
    } else {
        retMsg["type"] = "success";
        retMsg["message"] = "Settings saved!";
        retMsg["code"] = WebApiError::GenericSuccess;
    }
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
