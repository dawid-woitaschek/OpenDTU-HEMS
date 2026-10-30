// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <hems/storage/State.h>
#include <hems/warp/State.h>
namespace Remote {
void deviceSnapshot(JsonObject storage, JsonObject wallbox);
void storageJson(Hems::Storage::Snapshot const& state, JsonObject out);
void wallboxJson(Hems::Warp::Snapshot const& state, JsonObject out);
}
