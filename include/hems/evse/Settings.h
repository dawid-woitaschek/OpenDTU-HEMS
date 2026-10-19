// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ArduinoJson.h>
#include <cstring>
#include <hems/Control.h>
#include <hems/profiles/Profile.h>

namespace Hems::Evse {

struct BindingConfig {
    char Profile[Profiles::MaxId + 1] {};
    ControlMode Control = ControlMode::MonitorOnly;
};

inline bool deserializeBindingConfig(JsonVariantConst source, BindingConfig& target)
{
    target = {};
    if (source.isNull()) { return true; }
    if (!source.is<JsonObjectConst>() || !source["profile"].is<char const*>()) { return false; }

    auto profile = source["profile"].as<char const*>();
    auto length = std::strlen(profile);
    if (length > Profiles::MaxId) { return false; }
    if (length && !Profiles::validId(profile)) { return false; }

    if (!source["control_mode"].isNull()) {
        if (!source["control_mode"].is<char const*>()
                || !parseControlMode(source["control_mode"].as<char const*>(), target.Control)) {
            return false;
        }
    }

    std::memcpy(target.Profile, profile, length + 1);
    return true;
}

inline void serializeBindingConfig(BindingConfig const& source, JsonObject target)
{
    target["profile"] = static_cast<char const*>(source.Profile);
    target["control_mode"] = controlModeName(source.Control);
}

} // namespace Hems::Evse
