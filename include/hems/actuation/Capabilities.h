// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/actuation/Mailbox.h>
#include <hems/profiles/ControlPlan.h>

namespace Hems::Actuation {
struct OperationInfo {
    Action action = Action::Stop;
    bool hasInput = false;
    double minimum = 0, maximum = 0, step = 0;
    char unit[17] {};
};
struct ProfileInfo {
    char id[Profiles::MaxId + 1] {};
    char revision[Profiles::MaxText + 1] {};
    uint32_t fingerprint = 0;
    uint8_t schemaVersion = 0, operationCount = 0;
    bool telemetry = false, control = false, identity = false, recovery = false, releaseReplaySafe = false;
    char const* reason = "profile_not_loaded";
    OperationInfo operations[Profiles::MaxControlOperations + 1] {};
};
char const* operationKey(ControlTarget target, Action action);
bool planCommand(Profiles::Profile const& profile, Command const& command);
ProfileInfo describeProfile(Profiles::Profile const* profile, uint32_t fingerprint = 0);
uint32_t profileFingerprint(Profiles::Profile const& profile);
} // namespace Hems::Actuation
