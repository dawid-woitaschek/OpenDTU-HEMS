// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstring>

namespace FirmwareUpdateValidation {

inline char asciiLower(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

inline bool endsWithIgnoreCase(char const* value, char const* suffix)
{
    if (value == nullptr || suffix == nullptr) { return false; }

    auto const valueLength = std::strlen(value);
    auto const suffixLength = std::strlen(suffix);
    if (suffixLength > valueLength) { return false; }

    auto const offset = valueLength - suffixLength;
    for (std::size_t i = 0; i < suffixLength; ++i) {
        if (asciiLower(value[offset + i]) != asciiLower(suffix[i])) { return false; }
    }
    return true;
}

inline bool isFactoryImageFilename(char const* filename)
{
    return endsWithIgnoreCase(filename, ".factory.bin")
        || endsWithIgnoreCase(filename, ".factory.bin.gz");
}

} // namespace FirmwareUpdateValidation
