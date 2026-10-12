// SPDX-License-Identifier: GPL-2.0-or-later
#include <WebApi_profiles.h>
#include <WebApi.h>
#include <Configuration.h>
#include <defaults.h>
#include <LittleFS.h>
#include <hems/profiles/Store.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>

namespace {
using Hems::Profiles::Files;
using Hems::Profiles::Store;
using Hems::Profiles::Profile;
using Hems::Profiles::MaxImported;
using Hems::Profiles::MaxJson;
using Hems::Profiles::Schema;
using Hems::Profiles::Type;
using Hems::Profiles::parse;
using Hems::Profiles::serialize;
using Hems::Profiles::decode;
using Hems::Profiles::typeName;
class FlashFiles : public Files {
    static char const* path(size_t slot) {
        static char const* const paths[] = { "/hems-profiles/0.json", "/hems-profiles/1.json", "/hems-profiles/2.json", "/hems-profiles/3.json" };
        return slot < MaxImported ? paths[slot] : "";
    }
public:
    size_t size(size_t slot) override {
        if (!LittleFS.exists(path(slot))) { return 0; }
        auto f = LittleFS.open(path(slot), "r"); return f ? f.size() : SIZE_MAX;
    }
    bool read(size_t slot, char* buffer, size_t length) override {
        auto f = LittleFS.open(path(slot), "r"); return f && f.size() == length && f.readBytes(buffer, length) == length;
    }
    bool commit(size_t slot, char const* buffer, size_t length) override {
        constexpr char Temp[] = "/hems-profiles/pending.json";
        constexpr size_t Reserve = 8192;
        if (slot >= MaxImported || length > MaxJson) { return false; }
        LittleFS.remove(Temp); // recover an interrupted previous commit before checking space
        if (LittleFS.totalBytes() < LittleFS.usedBytes() + length + Reserve) { return false; }
        if (!LittleFS.exists("/hems-profiles") && !LittleFS.mkdir("/hems-profiles")) { return false; }
        auto f = LittleFS.open(Temp, "w");
        if (!f) { return false; }
        bool ok = f.write(reinterpret_cast<uint8_t const*>(buffer), length) == length;
        f.flush(); f.close();
        // Arduino File flush/close do not report errors. Verify the temporary
        // contents with a fixed small buffer before replacing the good file.
        auto verify = LittleFS.open(Temp, "r");
        ok = ok && verify && verify.size() == length;
        uint8_t chunk[128];
        for (size_t offset = 0; ok && offset < length;) {
            size_t n = std::min(sizeof(chunk), length - offset);
            ok = verify.read(chunk, n) == n && !memcmp(chunk, buffer + offset, n);
            offset += n;
        }
        verify.close();
        // LittleFS rename replaces the existing destination atomically. Never
        // unlink the good destination first; a power cut leaves old or new data.
        if (ok) { ok = LittleFS.rename(Temp, path(slot)); }
        if (!ok) { LittleFS.remove(Temp); }
        return ok;
    }
    bool remove(size_t slot) override { return slot < MaxImported && LittleFS.remove(path(slot)); }
};
FlashFiles files;
Store store(files);
std::mutex operation;
std::atomic<AsyncWebServerRequest*> uploadOwner { nullptr };
struct Upload { size_t received; bool valid; }; // buffer follows; request destructor frees it
void reply(AsyncWebServerRequest* request, int status, char const* error = nullptr) {
    auto* response = new AsyncJsonResponse();
    response->getRoot()["ok"] = error == nullptr;
    if (error) { response->getRoot()["message"] = error; }
    response->setCode(status); WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
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
    std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    char const* error = nullptr;
    auto json = reinterpret_cast<char*>(upload + 1);
    bool ok;
    if (persist) {
        auto replace = r->hasParam("replace") ? r->getParam("replace")->value().c_str() : nullptr;
        ok = store.import(json, upload->received, replace, error);
    } else {
        auto p = allocateProfile(); ok = p && parse(json, upload->received, *p, error);
        if (!p) { error = "Insufficient memory"; }
    }
    reply(r, ok ? 200 : 400, error);
}
void WebApiProfilesClass::list(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto p = allocateProfile(); if (!p) { reply(r, 503, "Insufficient memory"); return; }
    auto* response = new AsyncJsonResponse(); auto& root = response->getRoot();
    auto profiles = root["profiles"].to<JsonArray>(); char const* error = nullptr; size_t corrupt = 0;
    for (size_t i = 0; i <= MaxImported; ++i) {
        bool ok = i == 0 ? Store::builtin(*p, error) : store.load(i - 1, *p, error);
        if (!ok) { if (i && files.size(i - 1)) { ++corrupt; } continue; }
        // Reject ambiguous or illegally shadowed ids even if files were altered externally.
        bool builtIn; if (i && (!strcmp(p->id, "example.synthetic") || !store.get(p->id, *p, builtIn, error))) { ++corrupt; continue; }
        auto o = profiles.add<JsonObject>(); o["id"] = p->id; o["vendor"] = p->vendor; o["model"] = p->model;
        o["origin"] = i == 0 ? "Built-in" : "Imported"; o["schema"] = Schema;
        o["source"] = p->source; o["source_revision"] = p->revision;
        o["author"] = p->author; o["license"] = p->license; o["telemetry_count"] = p->entryCount;
        o["control_metadata"] = false; o["read_only"] = true;
    }
    root["corrupt_slots"] = corrupt; root["max_json_bytes"] = MaxJson; root["max_imported"] = MaxImported;
    WebApi.sendJsonResponse(r, response, __FUNCTION__, __LINE__);
}
void WebApiProfilesClass::exportProfile(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto p = allocateProfile(); if (!p) { reply(r, 503, "Insufficient memory"); return; }
    bool builtIn; char const* error;
    if (!store.get(id(r), *p, builtIn, error)) { reply(r, 404, error); return; }
    auto* response = new AsyncJsonResponse(); serialize(*p, response->getRoot().to<JsonObject>());
    response->addHeader("Content-Disposition", String("attachment; filename=\"") + p->id + ".json\"");
    WebApi.sendJsonResponse(r, response, __FUNCTION__, __LINE__);
}
void WebApiProfilesClass::erase(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    char const* error; bool ok = store.erase(id(r), error); reply(r, ok ? 200 : 400, error);
}
void WebApiProfilesClass::inspect(AsyncWebServerRequest* r) {
    if (!WebApi.checkCredentials(r)) { return; }
    std::unique_lock<std::mutex> lock(operation, std::try_to_lock);
    if (!lock.owns_lock()) { reply(r, 503, "Profile operation busy"); return; }
    auto p = allocateProfile(); if (!p) { reply(r, 503, "Insufficient memory"); return; }
    bool builtIn; char const* error;
    if (!store.get(id(r), *p, builtIn, error)) { reply(r, 404, error); return; }
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
