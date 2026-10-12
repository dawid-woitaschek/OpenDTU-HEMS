// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
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
