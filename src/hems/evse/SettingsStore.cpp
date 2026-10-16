// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/evse/SettingsStore.h>
#include <Configuration.h>
#include <LittleFS.h>
#include <esp_log.h>

#undef TAG
static const char* TAG = "hems-evse-config";

namespace Hems::Evse {
namespace {
constexpr char Path[] = "/hems-evse.json";
constexpr char Temp[] = "/hems-evse.tmp";
constexpr uint32_t Version = 1;
constexpr size_t MaxBytes = 512;

BindingConfig effectiveConfig(BindingConfig const& config)
{
    // Keep EVSE binding semantics symmetric with storage: a disabled consumer
    // owns no runtime profile and cannot keep an imported profile artificially
    // "in use". Re-enabling requires an explicit profile selection again.
    return Configuration.get().Hems.Warp.Enabled ? config : BindingConfig {};
}
} // namespace

SettingsStore BindingSettings;

void SettingsStore::init()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _config = {};
    if (!LittleFS.exists(Path)) { return; }

    auto file = LittleFS.open(Path, "r");
    if (!file || file.size() == 0 || file.size() > MaxBytes) {
        ESP_LOGW(TAG, "Ignoring invalid EVSE settings file");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        ESP_LOGW(TAG, "Ignoring malformed EVSE settings file");
        return;
    }

    BindingConfig candidate;
    if (!doc["version"].is<uint32_t>() || doc["version"].as<uint32_t>() != Version
            || !deserializeBindingConfig(doc["evse"], candidate)) {
        ESP_LOGW(TAG, "Ignoring unsupported or invalid EVSE settings");
        return;
    }
    _config = effectiveConfig(candidate);
}

BindingConfig SettingsStore::get() const
{
    bool enabled = Configuration.get().Hems.Warp.Enabled;
    std::lock_guard<std::mutex> lock(_mutex);
    return enabled ? _config : BindingConfig {};
}

bool SettingsStore::set(BindingConfig const& config)
{
    bool enabled = Configuration.get().Hems.Warp.Enabled;
    // Enabled EVSE telemetry/control always requires an explicit imported profile.
    if (enabled && !config.Profile[0]) { return false; }
    return persist(enabled ? config : BindingConfig {});
}

bool SettingsStore::clear() { return persist(BindingConfig {}); }

bool SettingsStore::persist(BindingConfig const& effective)
{
    JsonDocument doc;
    doc["version"] = Version;
    serializeBindingConfig(effective, doc["evse"].to<JsonObject>());
    if (doc.overflowed() || measureJson(doc) > MaxBytes) { return false; }

    LittleFS.remove(Temp);
    auto file = LittleFS.open(Temp, "w");
    if (!file || serializeJson(doc, file) == 0) {
        if (file) { file.close(); }
        LittleFS.remove(Temp);
        return false;
    }
    file.flush();
    file.close();

    auto verify = LittleFS.open(Temp, "r");
    JsonDocument verified;
    BindingConfig candidate;
    bool ok = verify && !deserializeJson(verified, verify)
        && verified["version"].is<uint32_t>() && verified["version"].as<uint32_t>() == Version
        && deserializeBindingConfig(verified["evse"], candidate);
    verify.close();
    if (!ok) {
        LittleFS.remove(Temp);
        return false;
    }

    if (!LittleFS.rename(Temp, Path)) {
        LittleFS.remove(Temp);
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _config = candidate;
    return true;
}

} // namespace Hems::Evse
