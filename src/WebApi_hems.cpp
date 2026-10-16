// SPDX-License-Identifier: GPL-2.0-or-later
#include "WebApi_hems.h"
#include "Configuration.h"
#include "WebApi.h"
#include <hems/ConfigUpdate.h>
#include <hems/Controller.h>
#include <hems/profiles/FlashStore.h>
#include <hems/storage/SettingsStore.h>
#include <hems/storage/Telemetry.h>
#include <memory>
#include <mutex>
#include <new>

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

char const* qualityName(Hems::Model::Quality quality)
{
    switch (quality) {
        case Hems::Model::Quality::Fresh: return "fresh";
        case Hems::Model::Quality::Stale: return "stale";
        case Hems::Model::Quality::Unknown:
        default: return "unknown";
    }
}

template <typename T>
void serializeSignal(JsonObject target, Hems::Model::Signal<T> const& signal)
{
    target["quality"] = qualityName(signal.quality);
    if (signal.known()) {
        target["value"] = signal.value;
    } else {
        target["value"] = nullptr;
    }
}

void serializeDevice(JsonObject target, Hems::Model::DeviceState const& device)
{
    target["configured"] = device.configured;
    target["enabled"] = device.enabled;
    target["reachable"] = device.reachable;
    target["has_update"] = device.hasUpdate;
    target["age_ms"] = nullptr;
    if (device.hasUpdate) { target["age_ms"] = device.ageMs; }
}

void serializeLimit(JsonObject target, Hems::Constraints::UpperLimit const& limit)
{
    target["active"] = limit.active;
    target["value_w"] = nullptr;
    if (limit.active) { target["value_w"] = limit.valueW; }
}

void serializeActuationTarget(JsonObject target, Hems::ControlTarget controlTarget)
{
    auto readiness = HemsController.getActuationReadiness(controlTarget);
    auto command = HemsController.getActuationCommandSnapshot(controlTarget);
    auto cleanup = HemsController.getActuationCleanupSnapshot(controlTarget);

    char profileId[Hems::Profiles::MaxId + 1] {};
    HemsController.getControlProfileId(controlTarget, profileId, sizeof(profileId));
    target["profile"] = static_cast<char const*>(profileId);
    target["control_mode"] = Hems::controlModeName(HemsController.getControlMode(controlTarget));
    target["permission_granted"] = HemsController.controlPermissionGranted(controlTarget);
    target["allowed"] = readiness.allowed();
    target["ready_for_active"] = readiness.readyForActive();

    auto blockers = target["blockers"].to<JsonArray>();
    for (auto blocker : {
            Hems::Actuation::ReadinessBlocker::DevicePermissionMissing,
            Hems::Actuation::ReadinessBlocker::DeviceNotConfigured,
            Hems::Actuation::ReadinessBlocker::DeviceNotEnabled,
            Hems::Actuation::ReadinessBlocker::ProfileNotControllable,
            Hems::Actuation::ReadinessBlocker::DeviceUnreachable,
            Hems::Actuation::ReadinessBlocker::TelemetryNotFresh,
            Hems::Actuation::ReadinessBlocker::CommandBusy,
        }) {
        if (readiness.has(blocker)) { blockers.add(Hems::Actuation::readinessBlockerName(blocker)); }
    }
    auto info = HemsController.getProfileInfo(controlTarget);
    auto profile = target["profile_info"].to<JsonObject>();
    profile["id"] = static_cast<char const*>(info.id);
    profile["schema_version"] = info.schemaVersion;
    profile["telemetry_supported"] = info.telemetry;
    profile["control_supported"] = info.control;
    profile["control_reason"] = info.reason;
    profile["identity_metadata"] = info.identity;
    profile["recovery_metadata"] = info.recovery;
    profile["release_replay_safe"] = info.releaseReplaySafe;
    profile["source_revision"] = static_cast<char const*>(info.revision);
    profile["fingerprint"] = info.fingerprint;
    auto operations = profile["operations"].to<JsonArray>();
    for (uint8_t i = 0; i < info.operationCount; ++i) {
        auto const& operation = info.operations[i];
        auto entry = operations.add<JsonObject>();
        entry["action"] = Hems::Actuation::actionName(operation.action);
        entry["has_input"] = operation.hasInput;
        if (operation.hasInput) {
            entry["min"] = operation.minimum; entry["max"] = operation.maximum; entry["step"] = operation.step;
            entry["unit"] = static_cast<char const*>(operation.unit);
        }
    }

    auto commandResult = HemsController.getActuationCommandResult(controlTarget);
    target["ownership_uncertain"] = HemsController.ownershipUncertain(controlTarget);
    JsonObject mailbox = target["command"].to<JsonObject>();
    mailbox["last_error"] = Hems::Actuation::commandErrorName(commandResult.error);
    mailbox["last_stage"] = Hems::Actuation::commandStageName(commandResult.stage);
    mailbox["execution_error"] = Hems::Actuation::executionErrorName(commandResult.execution.error);
    mailbox["transport_error"] = Hems::Modbus::errorName(commandResult.execution.transport.error);
    mailbox["planning_error"] = Hems::Profiles::controlPlanErrorName(commandResult.planning.error);
    mailbox["write_attempted"] = commandResult.execution.writeAttempted;
    mailbox["readback_verified"] = commandResult.execution.readbackVerified;
    mailbox["modbus_exception"] = commandResult.execution.transport.exception;
    mailbox["completed_ms"] = commandResult.completedMs;
    mailbox["age_ms"] = commandResult.completedMs ? millis() - commandResult.completedMs : 0;
    mailbox["automatic"] = command.command.automatic;
    mailbox["state"] = Hems::Actuation::mailboxStateName(command.state);
    mailbox["id"] = nullptr;
    mailbox["generation"] = nullptr;
    mailbox["action"] = nullptr;
    mailbox["value"] = nullptr;
    if (command.id != 0) {
        mailbox["id"] = command.id;
        mailbox["generation"] = command.generation;
        mailbox["action"] = Hems::Actuation::actionName(command.command.action);
        if (command.command.hasValue) { mailbox["value"] = command.command.value; }
    }

    JsonObject cleanupJson = target["cleanup"].to<JsonObject>();
    cleanupJson["state"] = Hems::Actuation::cleanupStateName(cleanup.state);
    cleanupJson["retained_context"] = cleanup.retainedContext;
    cleanupJson["attempts"] = cleanup.attempts;
    cleanupJson["last_error"] = Hems::Actuation::commandErrorName(cleanup.lastResult.error);
    cleanupJson["last_stage"] = Hems::Actuation::commandStageName(cleanup.lastResult.stage);
    cleanupJson["execution_error"] = Hems::Actuation::executionErrorName(cleanup.lastResult.execution.error);
    cleanupJson["transport_error"] = Hems::Modbus::errorName(cleanup.lastResult.execution.transport.error);
    cleanupJson["modbus_exception"] = cleanup.lastResult.execution.transport.exception;
}

} // namespace

void WebApiHemsClass::init(AsyncWebServer& server)
{
    initEnergy(server);
    using std::placeholders::_1;
    server.on("/api/hems/command", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onCommand, this, _1)));
    server.on("/api/hems/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onStatus, this, _1)));
    server.on("/api/hems/config", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onConfigGet, this, _1)));
    server.on("/api/hems/config", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiHemsClass::onConfigPost, this, _1)));
}

void WebApiHemsClass::onStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) { return; }

    auto* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["state"] = Hems::modeName(HemsController.getMode());
    root["decision"] = "None";
    root["uptime_ms"] = millis();
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
        auto flag = [&storage](char const* key, Hems::Storage::BoolValue const& value) {
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
        flag("charging", sample.charging);
        flag("discharging", sample.discharging);
        flag("fault_active", sample.faultActive);
        flag("alarm_active", sample.alarmActive);
    }

    auto normalizedState = HemsController.getNormalizedState();
    JsonObject normalized = root["normalized"].to<JsonObject>();
    JsonObject grid = normalized["grid"].to<JsonObject>();
    serializeSignal(grid["power_w"].to<JsonObject>(), normalizedState.grid.powerW);
    JsonObject pv = normalized["pv"].to<JsonObject>();
    serializeSignal(pv["power_w"].to<JsonObject>(), normalizedState.pv.powerW);

    JsonObject normalizedStorage = normalized["storage"].to<JsonObject>();
    serializeDevice(normalizedStorage["device"].to<JsonObject>(), normalizedState.storage.device);
    serializeSignal(normalizedStorage["soc_pct"].to<JsonObject>(), normalizedState.storage.socPct);
    serializeSignal(normalizedStorage["ac_power_w"].to<JsonObject>(), normalizedState.storage.acPowerW);
    serializeSignal(normalizedStorage["dc_power_w"].to<JsonObject>(), normalizedState.storage.dcPowerW);
    serializeSignal(normalizedStorage["battery_voltage_v"].to<JsonObject>(), normalizedState.storage.batteryVoltageV);
    serializeSignal(normalizedStorage["battery_current_a"].to<JsonObject>(), normalizedState.storage.batteryCurrentA);
    serializeSignal(normalizedStorage["temperature_c"].to<JsonObject>(), normalizedState.storage.temperatureC);
    serializeSignal(normalizedStorage["charging"].to<JsonObject>(), normalizedState.storage.charging);
    serializeSignal(normalizedStorage["discharging"].to<JsonObject>(), normalizedState.storage.discharging);
    serializeSignal(normalizedStorage["fault_active"].to<JsonObject>(), normalizedState.storage.faultActive);
    serializeSignal(normalizedStorage["alarm_active"].to<JsonObject>(), normalizedState.storage.alarmActive);

    JsonObject evse = normalized["evse"].to<JsonObject>();
    serializeDevice(evse["device"].to<JsonObject>(), normalizedState.evse.device);
    serializeSignal(evse["vehicle_connected"].to<JsonObject>(), normalizedState.evse.vehicleConnected);
    serializeSignal(evse["charging"].to<JsonObject>(), normalizedState.evse.charging);
    serializeSignal(evse["power_w"].to<JsonObject>(), normalizedState.evse.powerW);
    serializeSignal(evse["allowed_current_a"].to<JsonObject>(), normalizedState.evse.allowedCurrentA);

    auto constraints = HemsController.getConstraints();
    JsonObject constraintsJson = root["constraints"].to<JsonObject>();
    JsonObject storageConstraints = constraintsJson["storage"].to<JsonObject>();
    storageConstraints["charge_allowed"] = constraints.storage.chargeAllowed;
    storageConstraints["discharge_allowed"] = constraints.storage.dischargeAllowed;
    serializeLimit(storageConstraints["max_charge_power"].to<JsonObject>(), constraints.storage.maxChargePower);
    serializeLimit(storageConstraints["max_discharge_power"].to<JsonObject>(), constraints.storage.maxDischargePower);

    JsonObject actuation = root["actuation"].to<JsonObject>();
    actuation["global_active"] = HemsController.getMode() == Hems::Mode::Active;
    auto capability = HemsController.controlCapability();
    actuation["storage_control_enabled"] = capability.storage;
    actuation["evse_control_enabled"] = capability.evse;
    serializeActuationTarget(actuation["evse"].to<JsonObject>(), Hems::ControlTarget::Evse);
    serializeActuationTarget(actuation["storage"].to<JsonObject>(), Hems::ControlTarget::Storage);

    auto evProtection = HemsController.getEvProtectionShadowSnapshot();
    JsonObject shadow = root["shadow"].to<JsonObject>();
    JsonObject evProtectionJson = shadow["ev_protection"].to<JsonObject>();
    evProtectionJson["state"] = Hems::Policies::evProtectionStateName(evProtection.state);
    evProtectionJson["debounce_ms"] = Hems::Policies::EvProtectionDebounceMs;
    evProtectionJson["condition_age_ms"] = nullptr;
    if (evProtection.state != Hems::Policies::EvProtectionState::Inactive) {
        evProtectionJson["condition_age_ms"] = evProtection.conditionAgeMs;
    }
    evProtectionJson["charging_known"] = evProtection.chargingKnown;
    evProtectionJson["charging"] = nullptr;
    if (evProtection.chargingKnown) { evProtectionJson["charging"] = evProtection.charging; }
    evProtectionJson["would_block_storage_discharge"] =
        !evProtection.proposedConstraints.storage.dischargeAllowed;

    evProtectionJson["blocking_storage_discharge"] = !constraints.storage.dischargeAllowed;
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
    bool modePresent = !root["mode"].isNull();
    bool enabledPresent = !root["enabled"].isNull();
    bool warpPresent = !root["warp"].isNull();
    bool storagePresent = !root["storage"].isNull();

    auto sendValueError = [&](char const* error, char const* field, char const* message) {
        retMsg["error"] = error;
        retMsg["field"] = field;
        retMsg["message"] = message;
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
    };

    if (!root["version"].is<uint32_t>() || root["version"].as<uint32_t>() != CONFIG_VERSION_HEMS) {
        sendValueError("unsupported_version", "version", "Unsupported or missing HEMS configuration version.");
        return;
    }
    if (Configuration.get().Hems.Version != CONFIG_VERSION_HEMS) {
        sendValueError("unsupported_stored_version", "version", "Stored HEMS configuration version is unsupported.");
        return;
    }
    if (!modePresent && !enabledPresent && !warpPresent && !storagePresent) {
        sendValueError("empty_update", "request", "No HEMS settings were provided.");
        return;
    }
    if (Hems::ConfigUpdate::mixesPersistenceDomains(enabledPresent || modePresent, warpPresent, storagePresent)) {
        sendValueError("mixed_config_domains", "request",
                "Storage settings must be updated separately from HEMS and wallbox settings.");
        return;
    }
    if (enabledPresent && !root["enabled"].is<bool>()) {
        sendValueError("invalid_enabled", "enabled", "HEMS enabled must be true or false.");
        return;
    }
    if (warpPresent && !ConfigurationClass::deserializeWarpConfig(root["warp"], warpConfig)) {
        sendValueError("invalid_wallbox_settings", "warp",
                "Invalid wallbox settings. Verify enabled, hostname/IPv4 address and TCP port.");
        return;
    }
    if (storagePresent && !deserializeStorageConfig(root["storage"], storageConfig)) {
        sendValueError("invalid_storage_settings", "storage",
                "Invalid storage settings. Verify enabled, hostname/IPv4 address, profile, TCP port and Modbus unit ID.");
        return;
    }

    std::unique_lock<std::mutex> profileLock;
    char const* profileError = nullptr;
    if (storagePresent && !validateStorageProfile(storageConfig, profileLock, profileError)) {
        retMsg["error"] = "invalid_storage_profile";
        retMsg["field"] = "storage.profile";
        retMsg["message"] = profileError ? profileError : "Invalid storage profile";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (storagePresent && !Hems::Storage::StorageSettings.set(storageConfig)) {
        retMsg["error"] = "storage_persist_failed";
        retMsg["field"] = "storage";
        retMsg["message"] = "Failed to persist storage telemetry settings!";
        retMsg["code"] = WebApiError::GenericWriteFailed;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    if (profileLock.owns_lock()) { profileLock.unlock(); }

    bool mainConfigChanged = modePresent || enabledPresent || warpPresent;
    if (mainConfigChanged) {
        auto guard = Configuration.getWriteGuard();
        auto& config = guard.getConfig();
        if (enabledPresent) {
            config.Hems.Enabled = root["enabled"].as<bool>();
            config.Hems.Mode = config.Hems.Enabled ? Hems::Mode::Monitor : Hems::Mode::Disabled;
        }
        if (modePresent) {
            config.Hems.Mode = Hems::parseMode(root["mode"].as<char const*>());
            config.Hems.Enabled = config.Hems.Mode != Hems::Mode::Disabled;
        }
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

void WebApiHemsClass::onCommand(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) { return; }
    if (!request->hasParam("data", true)
            || request->getParam("data", true)->value().length() > 384) { request->send(400); return; }
    auto* response = new AsyncJsonResponse();
    Hems::Profiles::BoundedAllocator allocator; JsonDocument data(&allocator);
    if (!WebApi.parseRequestData(request, response, data)) { return; }
    auto name = data["target"].as<char const*>(); auto action = data["action"].as<char const*>();
    Hems::Actuation::Command command;
    bool valid = name && action && (!strcmp(name, "evse") || !strcmp(name, "storage"));
    if (valid) {
        command.target = !strcmp(name, "evse") ? Hems::ControlTarget::Evse : Hems::ControlTarget::Storage;
        bool found = false;
        for (auto candidate : { Hems::Actuation::Action::SetCurrentA, Hems::Actuation::Action::SetChargePowerW,
                Hems::Actuation::Action::SetDischargePowerW, Hems::Actuation::Action::SetTargetSocPct,
                Hems::Actuation::Action::Stop, Hems::Actuation::Action::Release }) {
            if (!strcmp(action, Hems::Actuation::actionName(candidate))) { command.action = candidate; found = true; break; }
        }
        valid = found;
        command.hasValue = !data["value"].isUnbound();
        if (command.hasValue) {
            valid = valid && data["value"].is<double>() && !data["value"].is<bool>();
            command.value = data["value"].as<double>();
        }
    }
    uint32_t id = 0;
    bool accepted = valid && HemsController.submitManual(command, id);
    auto& root = response->getRoot(); root["accepted"] = accepted; root["command_id"] = id;
    root["message"] = accepted ? "Command queued. Results and fresh telemetry follow in device status."
        : "Command unavailable: check device permission, freshness, supported bounds, current constraints and worker state.";
    response->setCode(accepted ? 202 : 409);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
