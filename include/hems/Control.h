// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <cstring>

namespace Hems {

enum class ControlMode : uint8_t {
    MonitorOnly,
    HemsControl,
};

enum class ControlTarget : uint8_t {
    Evse,
    Storage,
};

inline constexpr char const* controlModeName(ControlMode mode)
{
    switch (mode) {
        case ControlMode::HemsControl: return "hems";
        case ControlMode::MonitorOnly:
        default: return "monitor";
    }
}

inline bool parseControlMode(char const* value, ControlMode& mode)
{
    mode = ControlMode::MonitorOnly;
    if (!value) { return false; }
    if (!std::strcmp(value, "monitor")) { return true; }
    if (!std::strcmp(value, "hems")) {
        mode = ControlMode::HemsControl;
        return true;
    }
    return false;
}

} // namespace Hems
