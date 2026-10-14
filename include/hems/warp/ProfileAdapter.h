// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/evse/Telemetry.h>
#include <hems/warp/State.h>

namespace Hems::Warp {

// WARP-specific semantic validation layered on top of the generic profile
// decoder. This preserves the strict behavior of the legacy WARP poller.
bool fromProfileSample(Evse::Sample const& source, Sample& target);

} // namespace Hems::Warp
