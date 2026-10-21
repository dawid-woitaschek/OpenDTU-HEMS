// SPDX-License-Identifier: GPL-2.0-or-later
#include "scenario/Runtime.h"

int main()
{
    auto storageProfile = load("../device-profiles/storage/Marstek-Venus-E-3.0.json");
    auto evseProfile = load("../device-profiles/evse/Tinkerforge-WARP3-Smart.json");
    selectedProfiles[storageProfile.id] = storageProfile;
    selectedProfiles[evseProfile.id] = evseProfile;
    assert(!storageProfile.identityFieldCount && !storageProfile.releaseReplaySafe);
    auto& config = Configuration.data.Hems;
    config.Warp.Enabled = true; strcpy(config.Warp.Host, "warp.invalid");
    StorageConfig storage; storage.Enabled = true; strcpy(storage.Host, "storage.invalid"); strcpy(storage.Profile, storageProfile.id);
    Evse::BindingConfig evse; strcpy(evse.Profile, evseProfile.id);

    // No implicit WARP binding, including restored no-profile legacy configurations.
    {
        StorageConfig none; Storage::StorageSettings.set(none);
        Evse::BindingConfig unbound; unbound.Control = ControlMode::HemsControl;
        Evse::BindingSettings.set(unbound); config.Mode = Mode::Monitor;
        Controller unconfigured; Scheduler schedule; unconfigured.init(schedule);
        schedule.execute(); runWorker(); schedule.execute(); runWorker();
        assert(!unconfigured.getWarpSnapshot().enabled && !unconfigured.getWarpSnapshot().valid);
        assert(unconfigured.getWarpSnapshot().successfulPolls == 0 && writes.empty());
        uint32_t id; Actuation::Command stop { ControlTarget::Evse, Actuation::Action::Stop };
        assert(!unconfigured.submitManual(stop, id));
    }

    auto storageWrites = [](uint16_t address, uint16_t value) {
        size_t count = 0;
        for (auto const& write : writes) {
            if (write.host == "storage.invalid" && write.address == address && write.value == value) { ++count; }
        }
        return count;
    };
    // EV protection restricts discharge without interrupting charging or idle storage.
    // Exercise all four permissions in both Monitor and Active through the production worker.
    for (auto mode : { Mode::Monitor, Mode::Active })
    for (bool storageControl : { false, true }) for (bool evseControl : { false, true })
    for (int16_t power : { -500, 500, 0 }) {
        tasks.clear(); writes.clear(); registers.clear(); inputRegisters.clear(); testNow = 1000;
        writeFails = readFails = false; NetworkSettings.connected = true; freshRegisters();
        registers[30006] = static_cast<uint16_t>(-power); // Profile normalizes AC power with scale -1.
        storage.Control = storageControl ? ControlMode::HemsControl : ControlMode::MonitorOnly;
        evse.Control = evseControl ? ControlMode::HemsControl : ControlMode::MonitorOnly;
        Storage::StorageSettings.set(storage); Evse::BindingSettings.set(evse); config.Mode = mode;
        Controller controller; Scheduler scheduler; controller.init(scheduler);
        auto tick = [&] { ++testNow; scheduler.execute(); runWorker(); };
        tick(); tick(); tick();
        bool actionable = mode == Mode::Active && storageControl;
        bool intervened = actionable && power < 0;
        assert(!controller.getEvProtectionShadowSnapshot().proposedConstraints.storage.dischargeAllowed);
        assert(controller.getConstraints().storage.dischargeAllowed == !actionable);
        assert(storageWrites(42010, 0) == (intervened ? 1u : 0u));
        assert(storageWrites(42000, 21947) == 0);
        if (intervened) {
            auto command = controller.getActuationCommandSnapshot(ControlTarget::Storage);
            assert(command.command.automatic && command.command.action == Actuation::Action::Stop);
            registers[30006] = 0; // Observe the stopped storage on the next normal poll.
        }
        if (actionable) {
            uint32_t id;
            Actuation::Command discharge { ControlTarget::Storage, Actuation::Action::SetDischargePowerW, true, 500 };
            assert(!controller.submitManual(discharge, id));
            assert(!controller.submitActuation(discharge, id));
            if (!intervened) {
                Actuation::Command charge { ControlTarget::Storage, Actuation::Action::SetChargePowerW, true, 500 };
                assert(controller.submitManual(charge, id)); runWorker(); tick(); tick();
                // A manual charge owns the provider session but must not acquire an EV-protection release.
                assert(storageWrites(42010, 0) == 0);
            }
        }
        inputRegisters[1003] = 2;
        for (int i = 0; i < 26; ++i) { testNow += 5000; tick(); tick(); }
        assert(controller.getConstraints().storage.dischargeAllowed);
        assert(storageWrites(42010, 0) == (intervened ? 1u : 0u));
        assert(storageWrites(42000, 21947) == (intervened ? 1u : 0u));
    }

    // Release only an actual intervention, and never interrupt a subsequent manual charge.
    for (bool cancelledStop : { false, true }) {
        tasks.clear(); writes.clear(); registers.clear(); inputRegisters.clear(); testNow = 1000; freshRegisters();
        registers[30006] = 500; // Discharging.
        storage.Control = ControlMode::HemsControl; evse.Control = ControlMode::MonitorOnly;
        Storage::StorageSettings.set(storage); Evse::BindingSettings.set(evse); config.Mode = Mode::Monitor;
        Controller controller; Scheduler scheduler; controller.init(scheduler);
        auto tick = [&] { ++testNow; scheduler.execute(); runWorker(); };
        tick(); tick();
        if (cancelledStop) {
            uint32_t id;
            Actuation::Command charge { ControlTarget::Storage, Actuation::Action::SetChargePowerW, true, 500 };
            assert(controller.submitManual(charge, id)); runWorker(); tick(); tick();
            // Seed a prior write result: cancelling the later Stop must not attribute it to protection.
            assert(controller.getActuationCommandResult(ControlTarget::Storage).execution.writeAttempted);
        }
        config.Mode = Mode::Active; controller.updateSettings();
        ++testNow; scheduler.execute(); // Stop queued but not yet executed.
        assert(controller.getActuationCommandSnapshot(ControlTarget::Storage).state == Actuation::MailboxState::Pending);
        if (cancelledStop) { assert(controller.cancelPendingActuation(ControlTarget::Storage)); }
        runWorker(); registers[30006] = 0; tick();
        testNow += 5000; tick(); tick();
        if (!cancelledStop) {
            uint32_t id;
            Actuation::Command charge { ControlTarget::Storage, Actuation::Action::SetChargePowerW, true, 500 };
            assert(controller.submitManual(charge, id)); runWorker();
            registers[30006] = static_cast<uint16_t>(-500); // Fresh charging after manual override.
            tick(); tick(); testNow += 5000; tick(); tick();
            assert(controller.getStorageSnapshot().sample.acPowerW.value == 500);
        }
        inputRegisters[1003] = 2;
        for (int i = 0; i < 26; ++i) { testNow += 5000; tick(); tick(); }
        assert(storageWrites(42010, 0) == (cancelledStop ? 0u : 1u));
        assert(storageWrites(42000, 21947) == 0);
    }

    // Actual controller, policy, providers, mailbox, planner and executor for all four permissions.
    for (bool storageControl : { false, true }) for (bool evseControl : { false, true }) {
        tasks.clear(); writes.clear(); registers.clear(); testNow = 1000;
        writeFails = readFails = false; freshRegisters();
        storage.Control = storageControl ? ControlMode::HemsControl : ControlMode::MonitorOnly;
        evse.Control = evseControl ? ControlMode::HemsControl : ControlMode::MonitorOnly;
        Storage::StorageSettings.set(storage); Evse::BindingSettings.set(evse);
        config.Mode = Mode::Monitor;
        Controller controller; Scheduler scheduler; controller.init(scheduler);
        auto tick = [&] { ++testNow; scheduler.execute(); runWorker(); };
        tick(); tick();
        assert(controller.getWarpSnapshot().valid && controller.getStorageSnapshot().valid);
        assert(writes.empty()); // Monitor automatic producer stays inactive even while EV charges.
        auto proposal = controller.getEvProtectionShadowSnapshot();
        assert(!proposal.proposedConstraints.storage.dischargeAllowed);
        assert(controller.getConstraints().storage.dischargeAllowed);
        assert(controller.controlCapability().storage == storageControl);
        assert(controller.controlCapability().evse == evseControl);
        uint32_t id;
        Actuation::Command command { ControlTarget::Storage, Actuation::Action::SetChargePowerW, true, 500 };
        assert(controller.submitManual(command, id) == storageControl);
        runWorker(); tick(); tick();
        command = { ControlTarget::Evse, Actuation::Action::SetCurrentA, true, 6 };
        assert(controller.submitManual(command, id) == evseControl);
        runWorker(); tick(); tick();
        for (auto const& write : writes) {
            assert(write.host == "storage.invalid" ? storageControl : evseControl);
        }
        if (evseControl) { assert(controller.getActuationCommandResult(ControlTarget::Evse).execution.readbackVerified); }
        if (storageControl) { assert(controller.getActuationCommandResult(ControlTarget::Storage).execution.readbackVerified); }
        assert(!controller.submitActuation(command, id)); // Monitor never queues automatic writes.
        registers[30006] = 500; testNow += 5000; tick(); tick(); // Fresh actual discharge before Active.
        writes.clear(); config.Mode = Mode::Active; controller.updateSettings(); tick(); tick();
        assert(controller.getConstraints().storage.dischargeAllowed == !storageControl);
        if (storageControl) {
            assert(!writes.empty());
            assert(controller.getActuationCommandSnapshot(ControlTarget::Storage).command.action == Actuation::Action::Stop);
            for (auto const& write : writes) { assert(write.host == "storage.invalid"); }
        } else { assert(writes.empty()); }
        command = { ControlTarget::Evse, Actuation::Action::SetCurrentA, true, 10 };
        assert(controller.submitActuation(command, id) == evseControl); runWorker(); tick(); tick();
        command = { ControlTarget::Storage, Actuation::Action::Stop };
        assert(controller.submitActuation(command, id) == storageControl); runWorker(); tick(); tick();
        registers[30006] = 0; // Storage has stopped; no repeated intervention while idle.
        // Real stop-debounce release with continuously refreshed false EV telemetry.
        inputRegisters[1003] = 2;
        for (int i = 0; i < 26; ++i) { testNow += 5000; tick(); tick(); }
        assert(controller.getConstraints().storage.dischargeAllowed);
        if (storageControl) { assert(controller.getActuationCommandSnapshot(ControlTarget::Storage).command.action == Actuation::Action::Release); }
    }

    // Full mode x permission x target eligibility, including Disabled polling semantics.
    for (auto mode : { Mode::Disabled, Mode::Monitor, Mode::Active })
    for (bool storageControl : { false, true }) for (bool evseControl : { false, true }) {
        tasks.clear(); writes.clear(); registers.clear(); inputRegisters.clear(); testNow = 1000;
        writeFails = readFails = false; freshRegisters(); inputRegisters[1003] = 2;
        storage.Control = storageControl ? ControlMode::HemsControl : ControlMode::MonitorOnly;
        evse.Control = evseControl ? ControlMode::HemsControl : ControlMode::MonitorOnly;
        Storage::StorageSettings.set(storage); Evse::BindingSettings.set(evse); config.Mode = mode;
        Controller matrix; Scheduler schedule; matrix.init(schedule);
        auto advance = [&] { ++testNow; schedule.execute(); runWorker(); };
        advance(); advance();
        auto state = matrix.getNormalizedState();
        assert(state.evse.charging.known() == (mode != Mode::Disabled));
        assert(state.storage.socPct.known() == (mode != Mode::Disabled));
        assert(writes.empty());
        for (auto target : { ControlTarget::Storage, ControlTarget::Evse }) {
            bool permission = target == ControlTarget::Storage ? storageControl : evseControl;
            Actuation::Command command { target, Actuation::Action::Stop }; uint32_t id;
            assert(matrix.submitManual(command, id) == (permission && mode != Mode::Disabled));
            runWorker(); advance(); advance();
            assert(matrix.submitActuation(command, id) == (permission && mode == Mode::Active));
            runWorker(); advance(); advance();
            if (permission && mode != Mode::Disabled) {
                testNow += 100000; // No refresh: freshness alone revokes eligibility.
                assert(!matrix.submitManual(command, id));
                advance(); advance();
                assert(matrix.submitManual(command, id)); runWorker(); advance(); advance();
                readFails = true; testNow += 5000; advance(); advance();
                assert(!matrix.submitManual(command, id));
                readFails = false; testNow += 5000; advance(); advance();
                assert(matrix.submitManual(command, id)); runWorker(); advance(); advance();
            }
        }
        for (auto const& write : writes) {
            assert(mode != Mode::Disabled);
            assert(write.host == "storage.invalid" ? storageControl : evseControl);
        }
    }

    // Both production providers: failure is transient, permission change releases once,
    // failed old-endpoint release never locks the replacement, generation pins writes.
    tasks.clear(); writes.clear(); registers.clear(); testNow = 1000; freshRegisters();
    storage.Control = evse.Control = ControlMode::HemsControl;
    Storage::StorageSettings.set(storage); Evse::BindingSettings.set(evse); config.Mode = Mode::Monitor;
    Controller controller; Scheduler scheduler; controller.init(scheduler);
    auto tick = [&] { ++testNow; scheduler.execute(); runWorker(); };
    tick(); tick();
    for (auto target : { ControlTarget::Storage, ControlTarget::Evse }) {
        uint32_t id;
        Actuation::Command command { target, Actuation::Action::Stop };
        assert(controller.submitManual(command, id)); writeFails = true; runWorker();
        assert(controller.getActuationCommandSnapshot(target).state == Actuation::MailboxState::Failed);
        assert(!controller.submitManual(command, id)); // fresh telemetry naturally gates retry
        writeFails = false; tick(); tick();
        assert(controller.submitManual(command, id)); runWorker(); tick(); tick();
        command.action = Actuation::Action::Release;
        assert(controller.submitManual(command, id)); writeFails = true; runWorker();
        writeFails = false; tick(); tick();
        command.action = Actuation::Action::Stop;
        assert(controller.submitManual(command, id)); runWorker(); tick(); tick();
        // Failed permission-transition release is attempted once with old permission.
        writes.clear();
        if (target == ControlTarget::Storage) { storage.Control = ControlMode::MonitorOnly; Storage::StorageSettings.set(storage); }
        else { evse.Control = ControlMode::MonitorOnly; Evse::BindingSettings.set(evse); }
        controller.updateSettings(); writeFails = true; tick();
        auto cleanup = controller.getActuationCleanupSnapshot(target);
        assert(cleanup.state == Actuation::CleanupState::Failed && !cleanup.retainedContext && cleanup.attempts == 1);
        auto before = writes.size(); tick(); tick(); assert(writes.size() == before);
        writeFails = false;
        if (target == ControlTarget::Storage) { storage.Control = ControlMode::HemsControl; Storage::StorageSettings.set(storage); }
        else { evse.Control = ControlMode::HemsControl; Evse::BindingSettings.set(evse); }
        controller.updateSettings(); tick(); tick();
        assert(controller.submitManual(command, id));
        // Configuration race during first write retains exact old endpoint; later writes revoked.
        onWrite = [&] {
            if (target == ControlTarget::Storage) { strcpy(storage.Host, "new-storage.invalid"); Storage::StorageSettings.set(storage); }
            else { strcpy(config.Warp.Host, "new-warp.invalid"); }
            controller.updateSettings();
        };
        runWorker();
        assert(controller.getActuationCleanupSnapshot(target).retainedContext);
        writes.clear(); writeFails = true; tick();
        assert(!writes.empty() && writes.back().host == (target == ControlTarget::Storage ? "storage.invalid" : "warp.invalid"));
        writeFails = false; tick(); tick();
        assert(controller.submitManual(command, id)); runWorker(); tick(); tick();
        assert(writes.back().host == (target == ControlTarget::Storage ? "new-storage.invalid" : "new-warp.invalid"));
    }
    // Same-ID runtime replacement updates bound capabilities without reboot.
    auto old = evseProfile; old.controlOperationCount = old.controlWriteCount = old.releaseWriteCount = 0; old.schemaVersion = 1;
    selectedProfiles[old.id] = old; controller.updateSettings(); tick(); tick();
    assert(!strcmp(controller.getProfileInfo(ControlTarget::Evse).reason, "schema_v1_read_only"));
    uint32_t id; Actuation::Command current { ControlTarget::Evse, Actuation::Action::SetCurrentA, true, 6 };
    assert(!controller.submitManual(current, id));
    old.schemaVersion = 2; selectedProfiles[old.id] = old; controller.updateSettings(); tick(); tick();
    assert(!strcmp(controller.getProfileInfo(ControlTarget::Evse).reason, "missing_control_metadata"));
    selectedProfiles[evseProfile.id] = evseProfile; controller.updateSettings(); tick(); tick();
    assert(controller.getProfileInfo(ControlTarget::Evse).control);
    assert(controller.submitManual(current, id)); runWorker();
    // An EV constraint becoming actionable mid-command revokes subsequent discharge writes.
    config.Mode = Mode::Monitor; controller.updateSettings(); tick(); tick();
    Actuation::Command discharge { ControlTarget::Storage, Actuation::Action::SetDischargePowerW, true, 500 };
    inputRegisters[1003] = 3; testNow += 5000; tick(); tick();
    assert(controller.submitManual(discharge, id));
    writes.clear();
    onWrite = [&] { config.Mode = Mode::Active; controller.updateSettings(); scheduler.execute(); };
    runWorker();
    assert(controller.getActuationCommandSnapshot(ControlTarget::Storage).state == Actuation::MailboxState::Failed);
    assert(controller.getActuationCommandResult(ControlTarget::Storage).execution.error == Actuation::ExecutionError::AuthorizationRevoked);
    assert(writes.size() == 1);
    for (auto const& write : writes) { assert(write.address != 42010); }
    // No raw semantic operation is accepted.
    auto invalid = discharge; invalid.action = static_cast<Actuation::Action>(255);
    assert(!controller.submitManual(invalid, id));
    std::cout << "Control permissions, Active EV protection, transient failure, release, generation and profile reload tests passed\n";
    return 0;
}
