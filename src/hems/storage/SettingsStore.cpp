// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/SettingsStore.h>
#include <LittleFS.h>
#include <esp_log.h>

#undef TAG
static const char* TAG = "hems-storage-config";

namespace Hems::Storage {
namespace {
constexpr char Path[] = "/hems-storage.json";
constexpr char Temp[] = "/hems-storage.tmp";
constexpr uint32_t Version = 1;
constexpr size_t MaxBytes = 1024;
}

SettingsStore StorageSettings;

void SettingsStore::init()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _config = {};
    if (!LittleFS.exists(Path)) { return; }

    auto file = LittleFS.open(Path, "r");
    if (!file || file.size() == 0 || file.size() > MaxBytes) {
        ESP_LOGW(TAG, "Ignoring invalid storage settings file");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, file)) {
        ESP_LOGW(TAG, "Ignoring malformed storage settings file");
        return;
    }

    StorageConfig candidate;
    if (!doc["version"].is<uint32_t>() || doc["version"].as<uint32_t>() != Version
            || !deserializeStorageConfig(doc["storage"], candidate)) {
        ESP_LOGW(TAG, "Ignoring unsupported or invalid storage settings");
        return;
    }
    _config = candidate;
}

StorageConfig SettingsStore::get() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _config;
}

bool SettingsStore::set(StorageConfig const& config)
{
    JsonDocument doc;
    doc["version"] = Version;
    serializeStorageConfig(config, doc["storage"].to<JsonObject>());
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
    StorageConfig candidate;
    bool ok = verify && !deserializeJson(verified, verify)
        && verified["version"].is<uint32_t>() && verified["version"].as<uint32_t>() == Version
        && deserializeStorageConfig(verified["storage"], candidate);
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

} // namespace Hems::Storage
