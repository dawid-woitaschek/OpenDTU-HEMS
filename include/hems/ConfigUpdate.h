// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Hems::ConfigUpdate {

inline bool mixesPersistenceDomains(bool enabledPresent, bool warpPresent,
        bool storagePresent)
{
    return storagePresent && (enabledPresent || warpPresent);
}

} // namespace Hems::ConfigUpdate
