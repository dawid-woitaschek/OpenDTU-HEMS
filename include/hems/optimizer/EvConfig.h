// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <cstdint>
namespace Hems::Optimizer {
struct EvConfig {
    bool enabled = false, pvPreferred = true, maxPriceEnabled = false;
    uint32_t deadline = 0, requiredWh = 0;
    uint16_t minCurrentA = 6, maxCurrentA = 16;
    // Zero means installation not configured, never an implicit three-phase assumption.
    uint16_t voltage = 0, phases = 0;
    int32_t maxPriceMicros = 0;
};
bool parseEvConfig(JsonVariantConst input, EvConfig& output);
void serializeEvConfig(EvConfig const& config, JsonObject output);
} // namespace Hems::Optimizer
