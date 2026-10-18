// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/energy/Provider.h>
#include <hems/model/State.h>
#include <hems/constraints/Constraints.h>
#include <hems/policies/StorageEconomic.h>
namespace Hems::Optimizer {
constexpr size_t MaxSteps = 256, MaxStates = 54;
struct Config {
    bool socBoundsConfigured = false;
    bool enabled = false, effectivePrice = true, exportConfigured = false;
    uint32_t horizonSeconds = 86400, capacityWh = 0, maxChargeW = 0, maxDischargeW = 0, baseLoadW = 0;
    uint16_t minSoc = 0, maxSoc = 100, chargePermille = 0, dischargePermille = 0, socStepPct = 5;
    int32_t exportMicros = 0;
    Policies::StorageEconomicPolicy policies;
};
bool parseConfig(JsonVariantConst json, Config& config);
void serializeConfig(Config const& config, JsonObject output);
struct Inputs {
    Energy::Series const* tariff = nullptr;
    Energy::Series const* forecast = nullptr;
    Energy::Status tariffStatus, forecastStatus;
    Model::StorageState storage;
    Constraints::StorageConstraints currentConstraints;
    uint32_t now = 0;
};
struct Step {
    uint32_t start = 0, end = 0, startWh = 0, endWh = 0;
    int32_t marketPrice = 0, importPrice = 0, usedPrice = 0, pvW = 0, batteryW = 0;
    bool hasImportPrice = false;
    int64_t gridImportWs = 0, gridExportWs = 0, costNano = 0;
};
struct Plan {
    Step steps[MaxSteps] {};
    // Only provenance survives calculation; normalized interval snapshots are scratch.
    char tariffSource[49] {}, forecastSource[49] {}, currency[4] {};
    uint16_t count = 0, states = 0;
    bool pvIncluded = false, partial = false;
    uint32_t generatedAt = 0, initialWh = 0, finalWh = 0, runtimeUs = 0;
    int64_t baselineNano = 0, optimizedNano = 0;
    char const* state = "not_ready";
    char const* blocker = nullptr;
    char const* partialReason = nullptr;
    char const* targetState = "disabled";
    uint32_t targetDeadline = 0, targetWh = 0;
};
// Pure normalized-input calculation. No controller, provider, mailbox, executor or device access.
bool calculate(Config const& config, Inputs const& inputs, Plan& output);
void serializePlan(Config const& config, Inputs const& inputs, Plan const& plan, JsonObject output, size_t limit = 24, size_t offset = 0);
} // namespace Hems::Optimizer
