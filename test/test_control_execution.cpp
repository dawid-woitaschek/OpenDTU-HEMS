// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/actuation/Executor.h>
#include <hems/profiles/ControlPlan.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace Hems;

namespace {

Profiles::Profile profile()
{
    Profiles::Profile p;
    p.schemaVersion = 2;
    p.controlWriteCount = 4;
    p.acquireFirstWrite = 0;
    p.acquireWriteCount = 1;
    p.releaseFirstWrite = 1;
    p.releaseWriteCount = 1;
    p.controlOperationCount = 2;

    p.controlWrites[0].address = 42000;
    p.controlWrites[0].type = Profiles::Type::Uint16;
    p.controlWrites[0].constant = 21930;

    p.controlWrites[1].address = 42000;
    p.controlWrites[1].type = Profiles::Type::Uint16;
    p.controlWrites[1].constant = 21947;

    std::strcpy(p.controlOperations[0].key, "set_current_a");
    p.controlOperations[0].hasInput = true;
    p.controlOperations[0].minimum = 6;
    p.controlOperations[0].maximum = 32;
    p.controlOperations[0].step = 1;
    p.controlOperations[0].firstWrite = 2;
    p.controlOperations[0].writeCount = 1;
    p.controlWrites[2].address = 1002;
    p.controlWrites[2].type = Profiles::Type::Uint32;
    p.controlWrites[2].usesInput = true;
    p.controlWrites[2].scale = 0.001; // semantic A = raw mA * 0.001

    std::strcpy(p.controlOperations[1].key, "stop");
    p.controlOperations[1].firstWrite = 3;
    p.controlOperations[1].writeCount = 1;
    p.controlWrites[3].address = 1002;
    p.controlWrites[3].type = Profiles::Type::Uint32;
    p.controlWrites[3].constant = 0;
    return p;
}

struct FakeIo {
    int writeCalls = 0;
    int readCalls = 0;
    int authorizationChecks = 0;
    int revokeAfterChecks = -1;
    int failWriteAt = -1;
    int failReadAt = -1;
    bool mismatch = false;
    uint16_t address = 0;
    uint16_t count = 0;
    uint16_t values[2] {};
};

bool fakeAuthorized(void* context)
{
    auto& io = *static_cast<FakeIo*>(context);
    int call = io.authorizationChecks++;
    return io.revokeAfterChecks < 0 || call < io.revokeAfterChecks;
}

Modbus::Result fakeWrite(void* context, uint16_t address, uint16_t count,
        uint16_t const* values)
{
    auto& io = *static_cast<FakeIo*>(context);
    int call = io.writeCalls++;
    if (call == io.failWriteAt) { return { Modbus::Error::Timeout }; }
    io.address = address;
    io.count = count;
    for (uint16_t i = 0; i < count; ++i) { io.values[i] = values[i]; }
    return {};
}

Modbus::Result fakeRead(void* context, uint16_t address, uint16_t count, uint8_t* payload)
{
    auto& io = *static_cast<FakeIo*>(context);
    int call = io.readCalls++;
    if (call == io.failReadAt) { return { Modbus::Error::Connection }; }
    assert(address == io.address && count == io.count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t value = io.values[i];
        payload[i * 2] = static_cast<uint8_t>(value >> 8);
        payload[i * 2 + 1] = static_cast<uint8_t>(value);
    }
    if (io.mismatch && count) { payload[count * 2 - 1] ^= 1; }
    return {};
}

} // namespace

int main()
{
    auto p = profile();
    Profiles::ControlPlan plan;

    auto result = Profiles::planAcquire(p, plan);
    assert(result && plan.count == 1);
    assert(plan.writes[0].address == 42000 && plan.writes[0].count == 1);
    assert(plan.writes[0].values[0] == 21930);

    result = Profiles::planRelease(p, plan);
    assert(result && plan.count == 1 && plan.writes[0].values[0] == 21947);

    result = Profiles::planOperation(p, "set_current_a", true, 6, plan);
    assert(result && plan.count == 1);
    assert(plan.writes[0].address == 1002 && plan.writes[0].count == 2);
    assert(plan.writes[0].values[0] == 0 && plan.writes[0].values[1] == 6000);

    assert(Profiles::planOperation(p, "set_current_a", false, 0, plan).error
            == Profiles::ControlPlanError::InputRequired);
    assert(Profiles::planOperation(p, "stop", true, 0, plan).error
            == Profiles::ControlPlanError::UnexpectedInput);
    assert(Profiles::planOperation(p, "missing", false, 0, plan).error
            == Profiles::ControlPlanError::OperationNotFound);
    assert(Profiles::planOperation(p, "set_current_a", true, 5, plan).error
            == Profiles::ControlPlanError::InputOutOfRange);
    assert(Profiles::planOperation(p, "set_current_a", true, 6.5, plan).error
            == Profiles::ControlPlanError::InputStepMismatch);
    assert(Profiles::planOperation(p, "set_current_a", true, NAN, plan).error
            == Profiles::ControlPlanError::InputNotFinite);

    p.controlOperations[0].step = 0.001;
    p.controlWrites[2].scale = 0.1;
    assert(Profiles::planOperation(p, "set_current_a", true, 6.001, plan).error
            == Profiles::ControlPlanError::RawNotIntegral);
    p.controlOperations[0].step = 1;
    p.controlWrites[2].scale = 1e-12;
    assert(Profiles::planOperation(p, "set_current_a", true, 6, plan).error
            == Profiles::ControlPlanError::RawOutOfRange);

    p = profile();
    assert(Profiles::planOperation(p, "set_current_a", true, 16, plan));
    FakeIo fake;
    Actuation::RegisterIo io { &fake, fakeWrite, fakeRead, fakeAuthorized };
    auto executed = Actuation::execute(io, plan);
    assert(executed && fake.writeCalls == 1 && fake.readCalls == 1);
    assert(fake.authorizationChecks == 1);
    assert(fake.address == 1002 && fake.count == 2 && fake.values[1] == 16000);

    fake = {};
    fake.revokeAfterChecks = 0;
    executed = Actuation::execute(io, plan);
    assert(executed.error == Actuation::ExecutionError::AuthorizationRevoked);
    assert(executed.writeIndex == 0 && fake.writeCalls == 0 && fake.readCalls == 0);

    Profiles::ControlPlan twoWrites = plan;
    twoWrites.count = 2;
    twoWrites.writes[1] = twoWrites.writes[0];
    twoWrites.writes[1].address = 1004;
    fake = {};
    fake.revokeAfterChecks = 1;
    executed = Actuation::execute(io, twoWrites);
    assert(executed.error == Actuation::ExecutionError::AuthorizationRevoked);
    assert(executed.writeIndex == 1 && fake.writeCalls == 1 && fake.readCalls == 1);

    fake = {};
    fake.failWriteAt = 0;
    executed = Actuation::execute(io, plan);
    assert(executed.error == Actuation::ExecutionError::WriteFailed);
    assert(executed.transport.error == Modbus::Error::Timeout && executed.writeIndex == 0);

    fake = {};
    fake.failReadAt = 0;
    executed = Actuation::execute(io, plan);
    assert(executed.error == Actuation::ExecutionError::ReadbackFailed);
    assert(executed.transport.error == Modbus::Error::Connection && executed.writeIndex == 0);

    fake = {};
    fake.mismatch = true;
    executed = Actuation::execute(io, plan);
    assert(executed.error == Actuation::ExecutionError::VerificationFailed);
    assert(executed.transport.error == Modbus::Error::InvalidData && executed.writeIndex == 0);

    fake = {};
    executed = Actuation::execute(io, plan, false);
    assert(executed && fake.writeCalls == 1 && fake.readCalls == 0);

    Actuation::RegisterIo invalid;
    assert(Actuation::execute(invalid, plan).error == Actuation::ExecutionError::InvalidIo);

    Profiles::Profile readonly;
    readonly.schemaVersion = 1;
    assert(Profiles::planAcquire(readonly, plan).error == Profiles::ControlPlanError::ControlUnavailable);

    std::cout << "Profile control planning and guarded execution tests passed\n";
}
