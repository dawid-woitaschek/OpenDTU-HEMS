// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <hems/model/State.h>
#include <hems/constraints/Constraints.h>
#include <hems/actuation/Readiness.h>
#include <cmath>
#include <type_traits>
namespace Remote {
uint32_t hemsPlanRevision();
bool hemsSnapshot(JsonObject out);
inline char const* qualityName(Hems::Model::Quality q) {
    return q == Hems::Model::Quality::Fresh ? "fresh" : q == Hems::Model::Quality::Stale ? "stale" : "unknown";
}
template<class T> void signalJson(Hems::Model::Signal<T> const& signal, JsonObject out) {
    bool finite = true;
    if constexpr (std::is_floating_point<T>::value) { finite = std::isfinite(signal.value); }
    out["quality"] = finite ? qualityName(signal.quality) : "unknown"; out["value"] = nullptr;
    if (signal.known() && finite) { out["value"] = signal.value; }
}
void normalizedJson(Hems::Model::SystemState const& state, JsonObject out);
void constraintsJson(Hems::Constraints::StorageConstraints const& constraints, JsonObject out);
void readinessJson(Hems::Actuation::Readiness readiness, JsonObject out);
} // namespace Remote
