// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <hems/modbus/Host.h>

constexpr size_t WARP_MAX_HOST_LENGTH = Hems::Modbus::MaxHostLength;

struct WarpConfig {
    bool Enabled = false;
    char Host[WARP_MAX_HOST_LENGTH + 1] {};
    uint16_t Port = 502;
};

inline bool validWarpHost(char const* host)
{
    return Hems::Modbus::validHost(host);
}
