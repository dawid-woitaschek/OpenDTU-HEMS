// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/energy/Provider.h>
#include <cstring>
namespace Hems::Energy {
// Free-form upstream provenance is not allowed to smuggle URL credentials.
inline char const* remoteSource(char const* source) {
    return !source ? "" : std::strstr(source, "://") || std::strpbrk(source, "?@=#") ? "redacted_source" : source;
}
// Explicit whitelist: never serialize Settings URLs, coordinates or HTTP bodies.
void remoteJson(Domain domain, char const* provider, Series const& series, Status const& status,
    uint32_t utc, JsonObject out, size_t limit = 96);
inline void remoteJson(Domain domain, Settings const& settings, Series const& series, Status const& status,
    uint32_t utc, JsonObject out, size_t limit = 96) { remoteJson(domain, sourceName(settings), series, status, utc, out, limit); }
} // namespace Hems::Energy
