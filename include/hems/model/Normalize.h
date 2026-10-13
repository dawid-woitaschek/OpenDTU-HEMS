// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/model/State.h>
#include <hems/storage/State.h>
#include <hems/warp/State.h>
#include <cstdint>
#include <optional>

namespace Hems::Model {

Signal<double> normalizeGridPower(std::optional<float> powerW, bool valid, bool hasUpdate);
Signal<double> normalizePvPower(double totalPowerW, uint8_t configuredSolarSources,
        uint8_t observedSolarSources, bool classificationComplete, bool anyStale);
SystemState normalize(Warp::Snapshot const& warp, Storage::Snapshot const& storage);

} // namespace Hems::Model
