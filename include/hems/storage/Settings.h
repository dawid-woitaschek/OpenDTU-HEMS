// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <hems/modbus/Host.h>
#include <hems/profiles/Profile.h>

struct StorageConfig {
    bool Enabled = false;
    char Host[Hems::Modbus::MaxHostLength + 1] {};
    char Profile[Hems::Profiles::MaxId + 1] {};
    // Zero means: use the profile transport default.
    uint16_t Port = 0;
    uint8_t Unit = 0;
};

inline bool validStorageProfileId(char const* id)
{
    return Hems::Profiles::validId(id);
}

inline bool deserializeStorageConfig(JsonVariantConst source, StorageConfig& target)
{
    target = {};
    if (source.isNull()) { return true; }
    if (!source.is<JsonObjectConst>() || !source["enabled"].is<bool>()
            || !source["host"].is<char const*>() || !source["profile"].is<char const*>()) {
        return false;
    }

    uint16_t port = 0;
    uint8_t unit = 0;
    if (!source["port"].isNull()) {
        if (!source["port"].is<uint16_t>()) { return false; }
        port = source["port"].as<uint16_t>();
    }
    if (!source["unit"].isNull()) {
        if (!source["unit"].is<uint8_t>()) { return false; }
        unit = source["unit"].as<uint8_t>();
        if (unit > 247) { return false; }
    }

    auto host = source["host"].as<char const*>();
    auto profile = source["profile"].as<char const*>();
    bool enabled = source["enabled"].as<bool>();
    auto hostLength = std::strlen(host);
    auto profileLength = std::strlen(profile);

    // Always enforce representation bounds before copying. Semantic endpoint/profile
    // validity only matters while the consumer is enabled: stale disabled values
    // must never prevent an operator from disabling the consumer safely.
    if (hostLength > Hems::Modbus::MaxHostLength || profileLength > Hems::Profiles::MaxId) { return false; }
    if (enabled && (!Hems::Modbus::validHost(host) || !validStorageProfileId(profile))) { return false; }

    target.Enabled = enabled;
    std::memcpy(target.Host, host, hostLength + 1);
    // A disabled consumer must not keep a hidden profile reference. This makes
    // disabling the consumer an explicit way to release a profile for deletion.
    if (enabled) { std::memcpy(target.Profile, profile, profileLength + 1); }
    target.Port = port;
    target.Unit = unit;
    return true;
}

inline void serializeStorageConfig(StorageConfig const& source, JsonObject target)
{
    target["enabled"] = source.Enabled;
    // Force pointer semantics instead of const char[N] literal detection so
    // ArduinoJson owns copies after this StorageConfig goes out of scope.
    target["host"] = static_cast<char const*>(source.Host);
    target["profile"] = static_cast<char const*>(source.Profile);
    target["port"] = source.Port;
    target["unit"] = source.Unit;
}
