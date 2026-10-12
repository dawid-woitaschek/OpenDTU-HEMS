// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/model/State.h>
#include <hems/storage/State.h>
#include <hems/warp/State.h>

namespace Hems::Model {

SystemState normalize(Warp::Snapshot const& warp, Storage::Snapshot const& storage);

} // namespace Hems::Model
