// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/ControlPlan.h>

#include <cmath>
#include <cstring>

namespace Hems::Profiles {
namespace {

bool closeToInteger(double value)
{
    if (!std::isfinite(value)) { return false; }
    double nearest = std::round(value);
    double tolerance = 1e-9 * std::fmax(1.0, std::fabs(value));
    return std::fabs(value - nearest) <= tolerance;
}

bool outside(double value, double minimum, double maximum)
{
    double magnitude = std::fmax(1.0, std::fmax(std::fabs(minimum), std::fabs(maximum)));
    double tolerance = 1e-9 * magnitude;
    return value < minimum - tolerance || value > maximum + tolerance;
}

ControlPlanResult encodeWrite(ControlWrite const& write, bool hasInput, double input,
        PlannedWrite& output, uint8_t writeIndex)
{
    output = {};
    output.address = write.address;

    double raw = 0;
    if (write.usesInput) {
        if (!hasInput || !std::isfinite(write.scale) || write.scale == 0 || !std::isfinite(write.offset)) {
            return { ControlPlanError::InvalidMetadata, writeIndex };
        }
        raw = (input - write.offset) / write.scale;
        if (!std::isfinite(raw)) { return { ControlPlanError::RawOutOfRange, writeIndex }; }
        if (!closeToInteger(raw)) { return { ControlPlanError::RawNotIntegral, writeIndex }; }
        raw = std::round(raw);
    } else {
        raw = static_cast<double>(write.constant);
    }

    uint32_t bits = 0;
    switch (write.type) {
        case Type::Uint16:
            if (raw < 0 || raw > 65535) { return { ControlPlanError::RawOutOfRange, writeIndex }; }
            bits = static_cast<uint16_t>(raw);
            output.count = 1;
            break;
        case Type::Int16:
            if (raw < -32768 || raw > 32767) { return { ControlPlanError::RawOutOfRange, writeIndex }; }
            bits = static_cast<uint16_t>(static_cast<int16_t>(raw));
            output.count = 1;
            break;
        case Type::Uint32:
            if (raw < 0 || raw > 4294967295.0) { return { ControlPlanError::RawOutOfRange, writeIndex }; }
            bits = static_cast<uint32_t>(raw);
            output.count = 2;
            break;
        case Type::Int32:
            if (raw < -2147483648.0 || raw > 2147483647.0) { return { ControlPlanError::RawOutOfRange, writeIndex }; }
            bits = static_cast<uint32_t>(static_cast<int32_t>(raw));
            output.count = 2;
            break;
        default:
            return { ControlPlanError::InvalidMetadata, writeIndex };
    }

    if (output.count == 1) {
        output.values[0] = static_cast<uint16_t>(bits);
    } else {
        // Schema-v2 control values use normal Modbus register byte order and
        // high-word-first ordering. Alternate ordering is intentionally not
        // guessed; it needs explicit schema metadata before being supported.
        output.values[0] = static_cast<uint16_t>(bits >> 16);
        output.values[1] = static_cast<uint16_t>(bits);
    }
    return {};
}

ControlPlanResult planWrites(Profile const& profile, uint8_t first, uint8_t count,
        bool hasInput, double input, ControlPlan& plan)
{
    plan = {};
    if (first > profile.controlWriteCount || count > profile.controlWriteCount - first
            || count > MaxControlWrites) {
        return { ControlPlanError::InvalidMetadata };
    }
    for (uint8_t i = 0; i < count; ++i) {
        auto result = encodeWrite(profile.controlWrites[first + i], hasInput, input,
                plan.writes[i], i);
        if (!result) {
            plan = {};
            return result;
        }
        ++plan.count;
    }
    return {};
}

} // namespace

char const* controlPlanErrorName(ControlPlanError error)
{
    switch (error) {
        case ControlPlanError::None: return "None";
        case ControlPlanError::ControlUnavailable: return "ControlUnavailable";
        case ControlPlanError::OperationNotFound: return "OperationNotFound";
        case ControlPlanError::InputRequired: return "InputRequired";
        case ControlPlanError::UnexpectedInput: return "UnexpectedInput";
        case ControlPlanError::InputNotFinite: return "InputNotFinite";
        case ControlPlanError::InputOutOfRange: return "InputOutOfRange";
        case ControlPlanError::InputStepMismatch: return "InputStepMismatch";
        case ControlPlanError::RawNotIntegral: return "RawNotIntegral";
        case ControlPlanError::RawOutOfRange: return "RawOutOfRange";
        case ControlPlanError::InvalidMetadata: return "InvalidMetadata";
    }
    return "InvalidMetadata";
}

ControlPlanResult planAcquire(Profile const& profile, ControlPlan& plan)
{
    plan = {};
    if (profile.schemaVersion < 2 || !hasControl(profile)) {
        return { ControlPlanError::ControlUnavailable };
    }
    return planWrites(profile, profile.acquireFirstWrite, profile.acquireWriteCount,
            false, 0, plan);
}

ControlPlanResult planRelease(Profile const& profile, ControlPlan& plan)
{
    plan = {};
    if (profile.schemaVersion < 2 || !hasControl(profile)) {
        return { ControlPlanError::ControlUnavailable };
    }
    return planWrites(profile, profile.releaseFirstWrite, profile.releaseWriteCount,
            false, 0, plan);
}

ControlPlanResult planOperation(Profile const& profile, char const* key,
        bool hasInput, double input, ControlPlan& plan)
{
    plan = {};
    if (profile.schemaVersion < 2 || !hasControl(profile)) {
        return { ControlPlanError::ControlUnavailable };
    }
    if (!key || !*key) { return { ControlPlanError::OperationNotFound }; }

    ControlOperation const* operation = nullptr;
    for (uint8_t i = 0; i < profile.controlOperationCount; ++i) {
        if (!std::strcmp(profile.controlOperations[i].key, key)) {
            operation = &profile.controlOperations[i];
            break;
        }
    }
    if (!operation) { return { ControlPlanError::OperationNotFound }; }
    if (operation->hasInput && !hasInput) { return { ControlPlanError::InputRequired }; }
    if (!operation->hasInput && hasInput) { return { ControlPlanError::UnexpectedInput }; }

    if (operation->hasInput) {
        if (!std::isfinite(input)) { return { ControlPlanError::InputNotFinite }; }
        if (!std::isfinite(operation->minimum) || !std::isfinite(operation->maximum)
                || !std::isfinite(operation->step) || operation->minimum > operation->maximum
                || operation->step <= 0) {
            return { ControlPlanError::InvalidMetadata };
        }
        if (outside(input, operation->minimum, operation->maximum)) {
            return { ControlPlanError::InputOutOfRange };
        }
        double steps = (input - operation->minimum) / operation->step;
        if (!closeToInteger(steps)) { return { ControlPlanError::InputStepMismatch }; }
    }

    return planWrites(profile, operation->firstWrite, operation->writeCount,
            hasInput, input, plan);
}

} // namespace Hems::Profiles
