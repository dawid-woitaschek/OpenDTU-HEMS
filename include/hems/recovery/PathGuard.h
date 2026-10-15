// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstring>
namespace Hems::Recovery {
// Reserve the namespace across generic upload/delete/reset, including aliases
// containing repeated separators or dot components. No filesystem I/O here.
inline bool protectedPath(char const* path) {
    if (!path) { return false; }
    constexpr char Prefix[] = "hems-recovery-";
    for (auto p = path; *p; ++p) {
        if ((p == path || p[-1] == '/' || p[-1] == '\\')
                && !std::strncmp(p, Prefix, sizeof(Prefix) - 1)) { return true; }
    }
    return false;
}
} // namespace Hems::Recovery
