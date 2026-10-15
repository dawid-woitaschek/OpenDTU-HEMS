// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Capabilities.h>
#include <cstring>

namespace Hems::Actuation {
char const* operationKey(ControlTarget target, Action action)
{
    switch (action) {
        case Action::SetCurrentA: return "set_current_a";
        case Action::SetChargePowerW: return "charge_power_w";
        case Action::SetDischargePowerW: return "discharge_power_w";
        case Action::SetTargetSocPct: return "target_soc_pct";
        case Action::Stop: return target == ControlTarget::Evse ? "stop_charging" : "stop";
        case Action::Release: return "release";
    }
    return "";
}

bool planCommand(Profiles::Profile const& profile, Command const& command)
{
    if (!validCommand(command) || !Profiles::hasCapability(profile,
            command.target == ControlTarget::Storage ? Profiles::Capability::Storage : Profiles::Capability::Evse)) { return false; }
    Profiles::ControlPlan plan;
    if (!Profiles::planRelease(profile, plan)) { return false; }
    if (command.action == Action::Release) { return true; }
    return Profiles::planAcquire(profile, plan) && Profiles::planOperation(profile,
        operationKey(command.target, command.action), command.hasValue, command.value, plan);
}

ProfileInfo describeProfile(Profiles::Profile const* profile, uint32_t fingerprint)
{
    ProfileInfo info;
    if (!profile) { return info; }
    std::strcpy(info.id, profile->id); std::strcpy(info.revision, profile->revision);
    info.schemaVersion = profile->schemaVersion;
    info.fingerprint = fingerprint;
    info.telemetry = profile->entryCount != 0;
    info.identity = profile->identityFieldCount != 0;
    info.recovery = profile->recoveryMetadata;
    info.releaseReplaySafe = profile->releaseReplaySafe;
    if (profile->schemaVersion < 2) { info.reason = "schema_v1_read_only"; return info; }
    if (!Profiles::hasControl(*profile)) { info.reason = "missing_control_metadata"; return info; }
    if (!profile->releaseWriteCount) { info.reason = "missing_release"; return info; }
    Profiles::ControlPlan plan;
    if (!Profiles::planRelease(*profile, plan) || !Profiles::planAcquire(*profile, plan)) {
        info.reason = "invalid_control_metadata"; return info;
    }
    auto target = Profiles::hasCapability(*profile, Profiles::Capability::Storage) ? ControlTarget::Storage : ControlTarget::Evse;
    for (auto action : { Action::SetCurrentA, Action::SetChargePowerW, Action::SetDischargePowerW,
            Action::SetTargetSocPct, Action::Stop }) {
        for (uint8_t i = 0; i < profile->controlOperationCount; ++i) {
            auto const& op = profile->controlOperations[i];
            if (std::strcmp(op.key, operationKey(target, action))) { continue; }
            Command command; command.target = target; command.action = action;
            command.hasValue = op.hasInput; command.value = op.minimum;
            if (!planCommand(*profile, command)) { info.reason = "invalid_control_metadata"; return info; }
            auto& output = info.operations[info.operationCount++];
            output.action = action; output.hasInput = op.hasInput;
            output.minimum = op.minimum; output.maximum = op.maximum; output.step = op.step;
            std::strcpy(output.unit, op.unit);
        }
    }
    info.operations[info.operationCount++].action = Action::Release;
    info.control = true; info.reason = "ready";
    return info;
}

uint32_t profileFingerprint(Profiles::Profile const& profile)
{
    // Diagnostic content version only; never an identity/authorization gate.
    struct HashWriter {
        uint32_t value = 2166136261u;
        size_t write(uint8_t c) { value = (value ^ c) * 16777619u; return 1; }
        size_t write(uint8_t const* data, size_t size) { for (size_t i = 0; i < size; ++i) { write(data[i]); } return size; }
    } writer;
    Profiles::BoundedAllocator allocator; JsonDocument doc(&allocator);
    Profiles::serialize(profile, doc.to<JsonObject>());
    if (doc.overflowed()) { return 0; }
    serializeJson(doc, writer);
    return writer.value;
}
} // namespace Hems::Actuation
