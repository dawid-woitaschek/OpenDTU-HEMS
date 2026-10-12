// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

namespace Hems::Modbus {

constexpr size_t MaxHostLength = 253;

inline bool validHost(char const* host)
{
    if (!host || !*host) { return false; }
    size_t total = 0;
    size_t label = 0;
    char previous = 0;
    for (auto p = host; *p; ++p) {
        if (++total > MaxHostLength) { return false; }
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

} // namespace Hems::Modbus
