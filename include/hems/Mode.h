// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/Control.h>
#include <cstdint>
#include <cstring>
namespace Hems {
enum class Mode : uint8_t { Disabled, Monitor, Active };
inline char const* modeName(Mode mode)
{
    switch (mode) {
        case Mode::Disabled: return "Disabled";
        case Mode::Active: return "Active";
        default: return "Monitor";
    }
}
inline Mode parseMode(char const* value)
{
    if (value && !std::strcmp(value, "Disabled")) { return Mode::Disabled; }
    if (value && !std::strcmp(value, "Active")) { return Mode::Active; }
    return Mode::Monitor;
}
struct ControlCapability {
    bool storage = false, evse = false;
    bool enabled(ControlTarget target) const { return target == ControlTarget::Storage ? storage : evse; }
};
} // namespace Hems
