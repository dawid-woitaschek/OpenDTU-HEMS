// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/evse/Actuator.h>
#include <hems/storage/Actuator.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace Hems;

namespace {

struct WireWrite {
    uint16_t address;
    std::vector<uint16_t> values;
};

struct FakeIo {
    std::vector<WireWrite> writes;
    bool failWrite = false;
    bool failRead = false;
    bool mismatch = false;
    bool authorized = true;
    int revokeAfterWrites = -1;
    uint16_t lastAddress = 0;
    uint16_t lastCount = 0;
    uint16_t lastValues[2] {};
};

Modbus::Result fakeWrite(void* context, uint16_t address, uint16_t count,
        uint16_t const* values)
{
    auto& io = *static_cast<FakeIo*>(context);
    WireWrite item;
    item.address = address;
    item.values.assign(values, values + count);
    io.writes.push_back(item);
    io.lastAddress = address;
    io.lastCount = count;
    for (uint16_t i = 0; i < count; ++i) { io.lastValues[i] = values[i]; }
    return io.failWrite ? Modbus::Result { Modbus::Error::Timeout } : Modbus::Result {};
}

Modbus::Result fakeRead(void* context, uint16_t address, uint16_t count, uint8_t* payload)
{
    auto& io = *static_cast<FakeIo*>(context);
    if (io.failRead) { return { Modbus::Error::Timeout }; }
    assert(address == io.lastAddress && count == io.lastCount);
    for (uint16_t i = 0; i < count; ++i) {
        payload[i * 2] = static_cast<uint8_t>(io.lastValues[i] >> 8);
        payload[i * 2 + 1] = static_cast<uint8_t>(io.lastValues[i]);
    }
    if (io.mismatch) { payload[count * 2 - 1] ^= 1; }
    return {};
}

bool fakeAuthorized(void* context)
{
    auto const& io = *static_cast<FakeIo*>(context);
    return io.authorized && (io.revokeAfterWrites < 0
            || io.writes.size() < static_cast<size_t>(io.revokeAfterWrites));
}

Profiles::Profile storageProfile()
{
    Profiles::Profile p;
    p.schemaVersion = 2;
    p.capabilities = Profiles::capabilityBit(Profiles::Capability::Storage);
    p.acquireFirstWrite = 0;
    p.acquireWriteCount = 1;
    p.releaseFirstWrite = 1;
    p.releaseWriteCount = 1;
    p.controlOperationCount = 4;
    p.controlWriteCount = 10;

    p.controlWrites[0].address = 42000;
    p.controlWrites[0].type = Profiles::Type::Uint16;
    p.controlWrites[0].constant = 21930;
    p.controlWrites[1].address = 42000;
    p.controlWrites[1].type = Profiles::Type::Uint16;
    p.controlWrites[1].constant = 21947;

    std::strcpy(p.controlOperations[0].key, "charge_power_w");
    p.controlOperations[0].hasInput = true;
    p.controlOperations[0].minimum = 0;
    p.controlOperations[0].maximum = 2500;
    p.controlOperations[0].step = 1;
    p.controlOperations[0].firstWrite = 2;
    p.controlOperations[0].writeCount = 2;
    p.controlWrites[2].address = 42020;
    p.controlWrites[2].type = Profiles::Type::Uint16;
    p.controlWrites[2].usesInput = true;
    p.controlWrites[3].address = 42010;
    p.controlWrites[3].type = Profiles::Type::Uint16;
    p.controlWrites[3].constant = 1;

    std::strcpy(p.controlOperations[1].key, "discharge_power_w");
    p.controlOperations[1].hasInput = true;
    p.controlOperations[1].minimum = 0;
    p.controlOperations[1].maximum = 2500;
    p.controlOperations[1].step = 1;
    p.controlOperations[1].firstWrite = 4;
    p.controlOperations[1].writeCount = 2;
    p.controlWrites[4].address = 42021;
    p.controlWrites[4].type = Profiles::Type::Uint16;
    p.controlWrites[4].usesInput = true;
    p.controlWrites[5].address = 42010;
    p.controlWrites[5].type = Profiles::Type::Uint16;
    p.controlWrites[5].constant = 2;

    std::strcpy(p.controlOperations[2].key, "stop");
    p.controlOperations[2].firstWrite = 6;
    p.controlOperations[2].writeCount = 3;
    for (uint8_t i = 0; i < 3; ++i) { p.controlWrites[6 + i].type = Profiles::Type::Uint16; }
    p.controlWrites[6].address = 42010;
    p.controlWrites[7].address = 42020;
    p.controlWrites[8].address = 42021;

    std::strcpy(p.controlOperations[3].key, "target_soc_pct");
    p.controlOperations[3].hasInput = true;
    p.controlOperations[3].minimum = 13;
    p.controlOperations[3].maximum = 100;
    p.controlOperations[3].step = 1;
    p.controlOperations[3].firstWrite = 9;
    p.controlOperations[3].writeCount = 1;
    p.controlWrites[9].address = 42011;
    p.controlWrites[9].type = Profiles::Type::Uint16;
    p.controlWrites[9].usesInput = true;
    return p;
}

Profiles::Profile evseProfile()
{
    Profiles::Profile p;
    p.schemaVersion = 2;
    p.capabilities = Profiles::capabilityBit(Profiles::Capability::Evse);
    p.releaseFirstWrite = 0;
    p.releaseWriteCount = 1;
    p.controlOperationCount = 2;
    p.controlWriteCount = 3;

    p.controlWrites[0].address = 1002;
    p.controlWrites[0].type = Profiles::Type::Uint32;
    p.controlWrites[0].constant = 32000;

    std::strcpy(p.controlOperations[0].key, "set_current_a");
    p.controlOperations[0].hasInput = true;
    p.controlOperations[0].minimum = 6;
    p.controlOperations[0].maximum = 32;
    p.controlOperations[0].step = 1;
    p.controlOperations[0].firstWrite = 1;
    p.controlOperations[0].writeCount = 1;
    p.controlWrites[1].address = 1002;
    p.controlWrites[1].type = Profiles::Type::Uint32;
    p.controlWrites[1].usesInput = true;
    p.controlWrites[1].scale = 0.001;

    std::strcpy(p.controlOperations[1].key, "stop_charging");
    p.controlOperations[1].firstWrite = 2;
    p.controlOperations[1].writeCount = 1;
    p.controlWrites[2].address = 1002;
    p.controlWrites[2].type = Profiles::Type::Uint32;
    return p;
}

} // namespace

int main()
{
    FakeIo fake;
    Actuation::RegisterIo io { &fake, fakeWrite, fakeRead };
    auto storage = storageProfile();
    Storage::Actuator battery;

    assert(battery.setChargePower(io, storage, 500));
    assert(battery.ownsControl());
    assert(fake.writes.size() == 3);
    assert(fake.writes[0].address == 42000 && fake.writes[0].values[0] == 21930);
    assert(fake.writes[1].address == 42020 && fake.writes[1].values[0] == 500);
    assert(fake.writes[2].address == 42010 && fake.writes[2].values[0] == 1);

    assert(battery.setDischargePower(io, storage, 600));
    assert(fake.writes.size() == 5); // no second acquire
    assert(fake.writes[3].address == 42021 && fake.writes[3].values[0] == 600);
    assert(fake.writes[4].address == 42010 && fake.writes[4].values[0] == 2);

    assert(battery.releaseControl(io, storage));
    assert(!battery.ownsControl());
    assert(fake.writes.back().address == 42000 && fake.writes.back().values[0] == 21947);

    size_t before = fake.writes.size();
    assert(battery.setTargetSoc(io, storage, 80));
    assert(fake.writes.size() == before + 2); // reacquire + target SoC
    assert(fake.writes.back().address == 42011 && fake.writes.back().values[0] == 80);

    before = fake.writes.size();
    assert(battery.stop(io, storage));
    assert(fake.writes.size() == before + 3);
    assert(fake.writes[before].address == 42010);
    assert(fake.writes[before + 1].address == 42020);
    assert(fake.writes[before + 2].address == 42021);

    auto invalid = battery.setChargePower(io, storage, 2501);
    assert(!invalid && invalid.error == Actuation::CommandError::PlanningFailed);
    assert(invalid.stage == Actuation::CommandStage::Operation);
    assert(invalid.planning.error == Profiles::ControlPlanError::InputOutOfRange);

    auto evse = evseProfile();
    Evse::Actuator charger;
    before = fake.writes.size();
    assert(charger.setCurrent(io, evse, 6));
    assert(charger.ownsControl());
    assert(fake.writes.size() == before + 1); // no acquire write
    assert(fake.writes.back().address == 1002 && fake.writes.back().values.size() == 2);
    assert(fake.writes.back().values[0] == 0 && fake.writes.back().values[1] == 6000);

    assert(charger.stop(io, evse));
    assert(fake.writes.back().address == 1002);
    assert(fake.writes.back().values[0] == 0 && fake.writes.back().values[1] == 0);

    before = fake.writes.size();
    assert(charger.releaseControl(io, evse));
    assert(!charger.ownsControl() && fake.writes.size() == before + 1);
    assert(fake.writes.back().address == 1002 && fake.writes.back().values.size() == 2);
    assert(fake.writes.back().values[0] == 0 && fake.writes.back().values[1] == 32000);

    before = fake.writes.size();
    auto wrongProfile = charger.setCurrent(io, storage, 6);
    assert(!wrongProfile && wrongProfile.planning.error == Profiles::ControlPlanError::ControlUnavailable);
    assert(fake.writes.size() == before);

    // Failed acquire/readback can be retried through the same actuator without acknowledgement.
    for (int failure = 0; failure < 3; ++failure) {
        FakeIo wire; wire.failWrite = failure == 0; wire.failRead = failure == 1; wire.mismatch = failure == 2;
        Actuation::RegisterIo failedIo { &wire, fakeWrite, fakeRead, fakeAuthorized };
        Storage::Actuator actuator;
        assert(!actuator.setChargePower(failedIo, storage, 500));
        assert(actuator.ownsControl());
        wire.failWrite = wire.failRead = wire.mismatch = false;
        assert(actuator.setChargePower(failedIo, storage, 500));
        wire.failWrite = true;
        assert(!actuator.releaseControl(failedIo, storage));
        wire.failWrite = false;
        auto writesBeforeRetry = wire.writes.size();
        assert(actuator.stop(failedIo, storage));
        assert(wire.writes.size() > writesBeforeRetry && wire.writes[writesBeforeRetry].address == storage.controlWrites[storage.acquireFirstWrite].address);
        assert(actuator.releaseControl(failedIo, storage));
        actuator.reset();
        assert(actuator.stop(failedIo, storage));
    }

    // An acquire revoked after an earlier write retains possible ownership.
    auto multiAcquire = storage;
    multiAcquire.acquireWriteCount = 2;
    FakeIo revokedWire;
    revokedWire.revokeAfterWrites = 1;
    Actuation::RegisterIo revokedIo { &revokedWire, fakeWrite, fakeRead, fakeAuthorized };
    Storage::Actuator revoked;
    auto revokedResult = revoked.setChargePower(revokedIo, multiAcquire, 500);
    assert(!revokedResult && revokedResult.execution.writeAttempted && revoked.ownsControl());
    assert(revokedResult.execution.error == Actuation::ExecutionError::AuthorizationRevoked);
    revokedWire.revokeAfterWrites = -1;
    assert(revoked.releaseRetainedControl(revokedIo, multiAcquire));

    // Rejection before the first acquire write does not invent ownership.
    revokedWire = {};
    revokedWire.authorized = false;
    Storage::Actuator denied;
    auto deniedResult = denied.stop(revokedIo, storage);
    assert(!deniedResult && !deniedResult.execution.writeAttempted);
    assert(!denied.ownsControl() && revokedWire.writes.empty());

    // A WARP operation may have taken effect even without a reply; no acquire
    // sequence exists, but its release must still remain available.
    FakeIo warpFailure;
    warpFailure.failWrite = true;
    Actuation::RegisterIo warpFailureIo { &warpFailure, fakeWrite, fakeRead };
    Evse::Actuator uncertainCharger;
    assert(!uncertainCharger.setCurrent(warpFailureIo, evse, 6));
    assert(uncertainCharger.ownsControl());
    uncertainCharger.reset();
    warpFailure.failWrite = false;
    assert(uncertainCharger.releaseRetainedControl(warpFailureIo, evse));
    assert(!uncertainCharger.ownsControl());

    // Missing release metadata must never become a successful no-op cleanup,
    // nor permit establishing control that cannot subsequently be relinquished.
    auto noRelease = storage;
    noRelease.releaseWriteCount = 0;
    FakeIo noReleaseWire;
    Actuation::RegisterIo noReleaseIo { &noReleaseWire, fakeWrite, fakeRead };
    Storage::Actuator unreleasable;
    auto blocked = unreleasable.stop(noReleaseIo, noRelease);
    assert(!blocked && blocked.stage == Actuation::CommandStage::Release);
    assert(!unreleasable.ownsControl() && noReleaseWire.writes.empty());
    assert(!unreleasable.releaseRetainedControl(noReleaseIo, noRelease));
    assert(noReleaseWire.writes.empty());

    // Reset remains harmless before ownership has ever been taken.
    Storage::Actuator cleanBattery;
    Evse::Actuator cleanCharger;
    cleanBattery.reset();
    assert(!cleanBattery.ownsControl());
    cleanCharger.reset();
    assert(!cleanCharger.ownsControl());

    std::cout << "Semantic storage and EVSE actuator tests passed\n";
}
