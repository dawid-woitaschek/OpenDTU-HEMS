// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

namespace Hems::Modbus {

constexpr size_t MaxHostLength = 253;

inline bool validIpv4(char const* host)
{
    if (!host || !*host) { return false; }

    unsigned parts = 1;
    unsigned value = 0;
    unsigned digits = 0;
    for (auto p = host; *p; ++p) {
        if (*p == '.') {
            if (digits == 0 || value > 255 || parts == 4) { return false; }
            ++parts;
            value = 0;
            digits = 0;
            continue;
        }
        if (*p < '0' || *p > '9' || ++digits > 3) { return false; }
        value = value * 10 + static_cast<unsigned>(*p - '0');
        if (value > 255) { return false; }
    }
    return parts == 4 && digits > 0 && value <= 255;
}

inline bool validHost(char const* host)
{
    if (!host || !*host) { return false; }

    // A dotted token consisting exclusively of digits and dots is clearly intended
    // to be an IPv4 address. Do not let malformed IPv4 input silently fall through
    // as a syntactically valid DNS hostname (for example 10.0.1.1107).
    bool numericDotted = false;
    bool onlyDigitsAndDots = true;
    for (auto p = host; *p; ++p) {
        if (*p == '.') {
            numericDotted = true;
        } else if (*p < '0' || *p > '9') {
            onlyDigitsAndDots = false;
            break;
        }
    }
    if (numericDotted && onlyDigitsAndDots) { return validIpv4(host); }

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
