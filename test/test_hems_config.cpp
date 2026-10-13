// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Settings.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

namespace {

JsonObject makeStorage(JsonDocument& doc, bool enabled, char const* host, char const* profile,
        int port, int unit)
{
    auto object = doc.to<JsonObject>();
    object["enabled"] = enabled;
    object["host"] = host;
    object["profile"] = profile;
    object["port"] = port;
    object["unit"] = unit;
    return object;
}

void testEnabledStorageBoundaries()
{
    StorageConfig config;

    JsonDocument minimum;
    makeStorage(minimum, true, "storage.local", "vendor.model-v1", 0, 0);
    assert(deserializeStorageConfig(minimum.as<JsonVariantConst>(), config));
    assert(config.Enabled);
    assert(!std::strcmp(config.Host, "storage.local"));
    assert(!std::strcmp(config.Profile, "vendor.model-v1"));
    assert(config.Port == 0);
    assert(config.Unit == 0);

    JsonDocument maximum;
    makeStorage(maximum, true, "10.0.4.20", "vendor.model-v1", 65535, 247);
    assert(deserializeStorageConfig(maximum.as<JsonVariantConst>(), config));
    assert(config.Port == 65535);
    assert(config.Unit == 247);
}

void testEnabledStorageRejectsSemanticGarbage()
{
    StorageConfig config;

    JsonDocument badHost;
    makeStorage(badHost, true, "131.143.9311.11", "vendor.model-v1", 502, 1);
    assert(!deserializeStorageConfig(badHost.as<JsonVariantConst>(), config));

    JsonDocument emptyHost;
    makeStorage(emptyHost, true, "", "vendor.model-v1", 502, 1);
    assert(!deserializeStorageConfig(emptyHost.as<JsonVariantConst>(), config));

    JsonDocument badProfile;
    makeStorage(badProfile, true, "storage.local", "Vendor Model", 502, 1);
    assert(!deserializeStorageConfig(badProfile.as<JsonVariantConst>(), config));

    JsonDocument emptyProfile;
    makeStorage(emptyProfile, true, "storage.local", "", 502, 1);
    assert(!deserializeStorageConfig(emptyProfile.as<JsonVariantConst>(), config));
}

void testStorageRejectsWrongTypesAndRanges()
{
    StorageConfig config;

    JsonDocument badPort;
    auto object = makeStorage(badPort, true, "storage.local", "vendor.model-v1", 502, 1);
    object["port"] = "502";
    assert(!deserializeStorageConfig(badPort.as<JsonVariantConst>(), config));

    JsonDocument portOverflow;
    makeStorage(portOverflow, true, "storage.local", "vendor.model-v1", 65536, 1);
    assert(!deserializeStorageConfig(portOverflow.as<JsonVariantConst>(), config));

    JsonDocument unitOverflow;
    makeStorage(unitOverflow, true, "storage.local", "vendor.model-v1", 502, 248);
    assert(!deserializeStorageConfig(unitOverflow.as<JsonVariantConst>(), config));

    JsonDocument wrongEnabled;
    object = makeStorage(wrongEnabled, true, "storage.local", "vendor.model-v1", 502, 1);
    object["enabled"] = "false";
    assert(!deserializeStorageConfig(wrongEnabled.as<JsonVariantConst>(), config));

    JsonDocument missingHost;
    object = missingHost.to<JsonObject>();
    object["enabled"] = false;
    object["profile"] = "";
    object["port"] = 0;
    object["unit"] = 0;
    assert(!deserializeStorageConfig(missingHost.as<JsonVariantConst>(), config));

    JsonDocument missingProfile;
    object = missingProfile.to<JsonObject>();
    object["enabled"] = false;
    object["host"] = "storage.local";
    object["port"] = 0;
    object["unit"] = 0;
    assert(!deserializeStorageConfig(missingProfile.as<JsonVariantConst>(), config));
}

void testDisabledStorageCanAlwaysBeDisabledWithSafeLeftovers()
{
    StorageConfig config;

    JsonDocument stale;
    makeStorage(stale, false, "http://old-storage/value", "Vendor Model", 0, 0);
    assert(deserializeStorageConfig(stale.as<JsonVariantConst>(), config));
    assert(!config.Enabled);
    assert(!std::strcmp(config.Host, "http://old-storage/value"));
    assert(config.Profile[0] == '\0');
    assert(config.Port == 0);
    assert(config.Unit == 0);

    JsonDocument numericGarbage;
    makeStorage(numericGarbage, false, "131.143.9311.11", "bad profile!", 65535, 247);
    assert(deserializeStorageConfig(numericGarbage.as<JsonVariantConst>(), config));
    assert(!config.Enabled);
    assert(!std::strcmp(config.Host, "131.143.9311.11"));
    assert(config.Profile[0] == '\0');

    JsonDocument oversizedHost;
    std::string host(Hems::Modbus::MaxHostLength + 1, 'a');
    makeStorage(oversizedHost, false, host.c_str(), "", 0, 0);
    assert(!deserializeStorageConfig(oversizedHost.as<JsonVariantConst>(), config));

    JsonDocument oversizedProfile;
    std::string profile(Hems::Profiles::MaxId + 1, 'a');
    makeStorage(oversizedProfile, false, "storage.local", profile.c_str(), 0, 0);
    assert(!deserializeStorageConfig(oversizedProfile.as<JsonVariantConst>(), config));
}

void testNullStorageUsesSafeDefaults()
{
    StorageConfig config;
    std::strcpy(config.Host, "old.local");
    std::strcpy(config.Profile, "old.profile");
    config.Enabled = true;
    config.Port = 502;
    config.Unit = 1;

    JsonDocument doc;
    assert(deserializeStorageConfig(doc.as<JsonVariantConst>(), config));
    assert(!config.Enabled);
    assert(config.Host[0] == '\0');
    assert(config.Profile[0] == '\0');
    assert(config.Port == 0);
    assert(config.Unit == 0);
}

} // namespace

int main()
{
    testEnabledStorageBoundaries();
    testEnabledStorageRejectsSemanticGarbage();
    testStorageRejectsWrongTypesAndRanges();
    testDisabledStorageCanAlwaysBeDisabledWithSafeLeftovers();
    testNullStorageUsesSafeDefaults();
    std::cout << "HEMS storage configuration validation tests passed\n";
}
