// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t WARP_MAX_HOST_LENGTH = 253;

struct WarpConfig {
    bool Enabled = false;
    char Host[WARP_MAX_HOST_LENGTH + 1] {};
    uint16_t Port = 502;
};

inline bool validWarpHost(char const* host)
{
    if (!host || !*host) { return false; }
    size_t total = 0;
    size_t label = 0;
    char previous = 0;
    for (auto p = host; *p; ++p) {
        if (++total > WARP_MAX_HOST_LENGTH) { return false; }
        if (*p == '.') {
            if (label == 0 || previous == '-') { return false; }
            label = 0;
        } else {
            bool alphanumeric = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')
                || (*p >= '0' && *p <= '9');
            if ((!alphanumeric && *p != '-') || (label == 0 && *p == '-') || ++label > 63) { return false; }
        }
        previous = *p;
    }
    return label > 0 && previous != '-';
}
