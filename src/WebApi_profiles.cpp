// SPDX-License-Identifier: GPL-2.0-or-later
#include <WebApi_profiles.h>
#include <WebApi.h>
#include <Configuration.h>
#include <defaults.h>
#include <hems/Controller.h>
#include <hems/evse/SettingsStore.h>
#include <hems/evse/Telemetry.h>
#include <hems/profiles/FlashStore.h>
#include <hems/storage/SettingsStore.h>
#include <hems/storage/Telemetry.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace {
using Hems::Profiles::Store;
using Hems::Profiles::Profile;
using Hems::Profiles::MaxImported;
using Hems::Profiles::MaxJson;
using Hems::Profiles::Type;
using Hems::Profiles::parse;
using Hems::Profiles::serialize;
using Hems::Profiles::decode;
using Hems::Profiles::typeName;
using Hems::Profiles::schemaName;
using Hems::Profiles::hasControl;
Store& store() { return Hems::Profiles::runtimeStore(); }
Hems::Profiles::Files& files() { return Hems::Profiles::runtimeFiles(); }
std::mutex& operation() { return Hems::Profiles::runtimeStoreMutex(); }
std::atomic<AsyncWebServerRequest*> uploadOwner { nullptr };
struct Upload { size_t received; bool valid; }; // buffer follows; request destructor frees it
void reply(AsyncWebServerRequest* request, int status, char const* error = nullptr) {
    auto* response = new AsyncJsonResponse();
    response->getRoot()["ok"] = error == nullptr;
    if (error) { response->getRoot()["message"] = error; }
    response->setCode(status); WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
void replyInUse(AsyncWebServerRequest* request, char const* profileId, bool storage, bool evse) {
    auto* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    root["ok"] = false;
    root["message"] = "Profile is still referenced by HEMS configuration";
    root["profile"] = profileId;
    auto references = root["references"].to<JsonArray>();
    if (storage) {
        auto reference = references.add<JsonObject>();
        reference["consumer"] = "storage";
        reference["location"] = "/hems/devices";
    }
    if (evse) {
        auto reference = references.add<JsonObject>();
        reference["consumer"] = "evse";
        reference["location"] = "/hems/devices";
    }
    response->setCode(409);
    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}
char const* id(AsyncWebServerRequest* r) { return r->hasParam("id") ? r->getParam("id")->value().c_str() : ""; }
std::unique_ptr<Profile> allocateProfile() { return std::unique_ptr<Profile>(new (std::nothrow) Profile()); }
} // namespace
void WebApiProfilesClass::init(AsyncWebServer& server) {
    // ESPAsyncWebServer's backward-compatible URI matcher treats a bare route
    // as a prefix for child paths. Keep the collection endpoint exact so the
    // /export and /decode handlers below are not shadowed by this first route.
    server.on("/api/hems/profiles", HTTP_GET, [this](auto* r) { list(r); })
        .setFilter([](AsyncWebServerRequest* r) { return r->url() == "/api/hems/profiles"; });
    server.on("/api/hems/profiles/export", HTTP_GET, [this](auto* r) { exportProfile(r); });
    server.on("/api/hems/profiles/delete", HTTP_POST, [this](auto* r) { erase(r); });
    server.on("/api/hems/profiles/decode", HTTP_GET, [this](auto* r) { inspect(r); });
    server.on("/api/hems/profiles/evse-binding", HTTP_GET, [this](auto* r) { bindingGet(r); });
    server.on("/api/hems/profiles/evse-binding", HTTP_POST, [this](auto* r) { bindingPost(r); });
    for (bool persist : { false, true }) {
        server.on(persist ? "/api/hems/profiles/import" : "/api/hems/profiles/validate", HTTP_POST,
            [this, persist](auto* r) { finish(r, persist); }, nullptr,
            [this](auto* r, auto* data, size_t len, size_t index, size_t total) { body(r, data, len, index, total); });
    }
}
void WebApiProfilesClass::body(AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
    // Authenticate before any application allocation; final handler sends 401.
    if (!r->authenticate(AUTH_USERNAME, Configuration.get().Security.Password)
            || r->contentType() != "application/json" || total == 0 || total > MaxJson || r->contentLength() != total) { return; }
    if (index == 0 && !r->_tempObject) {
        AsyncWebServerRequest* expected = nullptr;
        if (!uploadOwner.compare_exchange_strong(expected, r)) { return; }
        r->onDisconnect([r] { auto expected = r; uploadOwner.compare_exchange_strong(expected, nullptr); });
        r->_tempObject = malloc(sizeof(Upload) + total);
        if (!r->_tempObject) { return; }
        auto& upload = *static_cast<Upload*>(r->_tempObject); upload.received = 0; upload.valid = true;
    }
    auto* upload = static_cast<Upload*>(r->_tempObject);
    if (!upload || !upload->valid) { return; }
    if (index != upload->received || index > total || len > total - index) { upload->valid = false; return; }
    memcpy(reinterpret_cast<char*>(upload + 1) + index, data, len); upload->received += len;
}
void WebApiProfilesClass::finish(AsyncWebServerRequest* r, bool persist) {
    if (!WebApi.checkCredentials(r)) { return; }
    if (r->contentType() != "application/json") { reply(r, 415, "application/json required"); return; }
    if (r->contentLength() == 0 || r->contentLength() > MaxJson) { reply(r, 413, "Profile size limit: 6144 bytes"); return; }
    auto* upload = static_cast<Upload*>(r->_tempObject);
    if (!upload || uploadOwner.load() != r) { reply(r, 503, "Import busy or insufficient memory"); return; }
    if (!upload->valid || upload->received != r->contentLength()) { reply(r, 400, "Incomplete upload"); return; }
    std::unique_lock<std::mutex> lock(operation(), std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    char const* error = nullptr;
    auto json = reinterpret_cast<char*>(upload + 1);
    bool ok;
    if (persist) {
        auto replace = r->hasParam("replace") ? r->getParam("replace")->value().c_str() : nullptr;
        auto storageConfig = Hems::Storage::StorageSettings.get();
        auto evseConfig = Hems::Evse::BindingSettings.get();
        bool storageInUse = replace && storageConfig.Profile[0] && !strcmp(storageConfig.Profile, replace);
        bool evseInUse = replace && evseConfig.Profile[0] && !strcmp(evseConfig.Profile, replace);
        if (storageInUse || evseInUse) {
            auto candidate = allocateProfile();
            if (!candidate || !parse(json, upload->received, *candidate, error)) {
                if (!candidate) { error = "Insufficient memory"; }
                reply(r, 400, error);
                return;
            }
            if (storageInUse && !Hems::Storage::supportsProfile(*candidate)) {
                reply(r, 409, "Replacement profile is not compatible with storage telemetry");
                return;
            }
            if (evseInUse && !Hems::Evse::supportsProfile(*candidate)) {
                reply(r, 409, "Replacement profile is not compatible with EVSE telemetry");
                return;
            }
        }
        ok = store().import(json, upload->received, replace, error);
    } else {
        auto p = allocateProfile(); ok = p && parse(json, upload->received, *p, error);
        if (!p) { error = "Insufficient memory"; }
    }
    // Runtime providers may hold imported profiles. Reload only after the store
    // mutex is released, otherwise configure() would deadlock on the same store.
    if (ok && persist) {
        lock.unlock();
        HemsController.updateSettings();
    }
    reply(r, ok ? 200 : 400, error);
}
void WebApiProfilesClass::list(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation(), std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto p = allocateProfile(); if (!p) { reply(r, 503, "Insufficient memory"); return; }
    auto storageConfig = Hems::Storage::StorageSettings.get();
    auto evseConfig = Hems::Evse::BindingSettings.get();
    auto* response = new AsyncJsonResponse(); auto& root = response->getRoot();
    auto profiles = root["profiles"].to<JsonArray>(); char const* error = nullptr; size_t corrupt = 0;
    for (size_t i = 0; i <= MaxImported; ++i) {
        bool ok = i == 0 ? Store::builtin(*p, error) : store().load(i - 1, *p, error);
        if (!ok) { if (i && files().size(i - 1)) { ++corrupt; } continue; }
        // Reject ambiguous or illegally shadowed ids even if files were altered externally.
        bool builtIn; if (i && (!strcmp(p->id, "example.synthetic") || !store().get(p->id, *p, builtIn, error))) { ++corrupt; continue; }
        builtIn = i == 0;
        auto o = profiles.add<JsonObject>();
        o["id"] = static_cast<char const*>(p->id);
        o["vendor"] = static_cast<char const*>(p->vendor);
        o["model"] = static_cast<char const*>(p->model);
        o["origin"] = builtIn ? "Built-in" : "Imported";
        o["schema"] = schemaName(*p);
        o["source"] = static_cast<char const*>(p->source);
        o["source_revision"] = static_cast<char const*>(p->revision);
        o["author"] = static_cast<char const*>(p->author);
        o["license"] = static_cast<char const*>(p->license);
        o["telemetry_count"] = p->entryCount;
        o["control_metadata"] = hasControl(*p);
        o["read_only"] = !hasControl(*p);
        auto capabilities = o["capabilities"].to<JsonArray>();
        if (Hems::Storage::supportsProfile(*p)) { capabilities.add("storage"); }
        if (Hems::Evse::supportsProfile(*p)) { capabilities.add("evse"); }
        auto usedBy = o["used_by"].to<JsonArray>();
        if (storageConfig.Profile[0] && !strcmp(storageConfig.Profile, p->id)) { usedBy.add("storage"); }
        if (evseConfig.Profile[0] && !strcmp(evseConfig.Profile, p->id)) { usedBy.add("evse"); }
        o["in_use"] = usedBy.size() != 0;
    }
    root["corrupt_slots"] = corrupt; root["max_json_bytes"] = MaxJson; root["max_imported"] = MaxImported;
    WebApi.sendJsonResponse(r, response, __FUNCTION__, __LINE__);
}
void WebApiProfilesClass::exportProfile(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation(), std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto p = allocateProfile(); if (!p) { reply(r, 503, "Insufficient memory"); return; }
    bool builtIn; char const* error;
    if (!store().get(id(r), *p, builtIn, error)) { reply(r, 404, error); return; }
    auto* response = new AsyncJsonResponse(); serialize(*p, response->getRoot().to<JsonObject>());
    response->addHeader("Content-Disposition", String("attachment; filename=\"") + p->id + ".json\"");
    WebApi.sendJsonResponse(r, response, __FUNCTION__, __LINE__);
}
void WebApiProfilesClass::erase(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation(), std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto profileId = id(r);
    auto storageConfig = Hems::Storage::StorageSettings.get();
    auto evseConfig = Hems::Evse::BindingSettings.get();
    bool storageInUse = storageConfig.Profile[0] && !strcmp(storageConfig.Profile, profileId);
    bool evseInUse = evseConfig.Profile[0] && !strcmp(evseConfig.Profile, profileId);
    if (storageInUse || evseInUse) {
        replyInUse(r, profileId, storageInUse, evseInUse);
        return;
    }
    char const* error;
    bool ok = store().erase(profileId, error);
    if (ok) {
        lock.unlock();
        HemsController.updateSettings();
    }
    reply(r, ok ? 200 : 400, error);
}
void WebApiProfilesClass::inspect(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation(), std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto p = allocateProfile(); if (!p) { reply(r, 503, "Insufficient memory"); return; }
    bool builtIn; char const* error;
    if (!store().get(id(r), *p, builtIn, error)) { reply(r, 404, error); return; }
    if (!r->hasParam("key") || !r->hasParam("hex")) { reply(r, 400, "key and hex required"); return; }
    auto key = r->getParam("key")->value(); auto hex = r->getParam("hex")->value();
    uint8_t bytes[4] {}; size_t size = hex.length() / 2;
    if (hex.length() > 8 || hex.length() % 2 || size == 0) { reply(r, 400, "Expected 1..4 payload bytes in hex"); return; }
    auto digit = [](char c) -> int { if (c >= '0' && c <= '9') { return c - '0'; } if (c >= 'a' && c <= 'f') { return c - 'a' + 10; } if (c >= 'A' && c <= 'F') { return c - 'A' + 10; } return -1; };
    for (size_t i = 0; i < size; ++i) {
        int a = digit(hex[i * 2]), b = digit(hex[i * 2 + 1]);
        if (a < 0 || b < 0) { reply(r, 400, "Invalid hex"); return; } bytes[i] = a * 16 + b;
    }
    for (size_t i = 0; i < p->entryCount; ++i) {
        auto const& e = p->entries[i]; if (key != e.key) { continue; }
        auto value = decode(e, bytes, size); auto* response = new AsyncJsonResponse(); auto& root = response->getRoot();
        root["valid"] = value.valid; root["type"] = typeName(value.type); root["value"] = nullptr;
        if (value.valid) { if (value.type == Type::Bool) { root["value"] = value.boolean; } else { root["value"] = value.number; } }
        WebApi.sendJsonResponse(r, response, __FUNCTION__, __LINE__); return;
    }
    reply(r, 404, "Telemetry key not found");
}
