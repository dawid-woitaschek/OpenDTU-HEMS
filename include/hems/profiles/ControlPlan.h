// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <hems/profiles/Profile.h>

namespace Hems::Profiles {

enum class ControlPlanError : uint8_t {
    None,
    ControlUnavailable,
    OperationNotFound,
    InputRequired,
    UnexpectedInput,
    InputNotFinite,
    InputOutOfRange,
    InputStepMismatch,
    RawNotIntegral,
    RawOutOfRange,
    InvalidMetadata,
};

char const* controlPlanErrorName(ControlPlanError error);

struct PlannedWrite {
    uint16_t address = 0;
    uint8_t count = 0;
    uint16_t values[2] {};
};

struct ControlPlan {
    uint8_t count = 0;
    PlannedWrite writes[MaxControlWrites] {};
};

struct ControlPlanResult {
    ControlPlanError error = ControlPlanError::None;
    uint8_t writeIndex = 0xff;
    explicit operator bool() const { return error == ControlPlanError::None; }
};

ControlPlanResult planAcquire(Profile const& profile, ControlPlan& plan);
ControlPlanResult planRelease(Profile const& profile, ControlPlan& plan);
ControlPlanResult planOperation(Profile const& profile, char const* key,
        bool hasInput, double input, ControlPlan& plan);

} // namespace Hems::Profiles
