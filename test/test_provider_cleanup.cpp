// SPDX-License-Identifier: GPL-2.0-or-later
// Production provider dispatch/session/cleanup, with deterministic task and wire
// seams. No private-field access and no network threads or wall-clock waits.
#include "recovery_files.h"
#include <hems/storage/Provider.h>
#include <hems/warp/Provider.h>
#include <hems/profiles/FlashStore.h>
#include <Arduino.h>
#include <NetworkSettings.h>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct TestTask {
    void (*entry)(void*);
    void* context;
    bool notified = false;
};
static std::vector<std::unique_ptr<TestTask>> tasks;
static TestTask* currentTask;
struct WorkerIdle {};
int xTaskCreate(void (*entry)(void*), char const*, unsigned, void* context, int, TaskHandle_t* task)
{
    tasks.emplace_back(new TestTask { entry, context });
    *task = tasks.back().get();
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) { task->notified = true; }
uint32_t ulTaskNotifyTake(int, uint32_t)
{
    if (!currentTask->notified) { throw WorkerIdle {}; }
    currentTask->notified = false;
    return 1;
}
static void runWorker()
{
    for (auto& task : tasks) {
        if (!task->notified) { continue; }
        currentTask = task.get();
        try { task->entry(task->context); } catch (WorkerIdle const&) {}
    }
}

using namespace Hems;
static Profiles::Profile selectedProfile;
static std::function<void()> onWrite;
static std::function<void()> onIdentityRead;
struct Write { std::string host; uint16_t port; uint8_t unit; uint16_t address; };
static std::vector<Write> writes;
static std::string wireHost;
static uint16_t wirePort;
static uint8_t wireUnit;
static std::map<uint16_t, uint16_t> registers;
static bool writeFails;
static bool delayedFailure;

namespace Hems::Profiles {
// Profile loading is stubbed; planning/execution and provider state are real.
static class EmptyFiles : public Files {
    size_t size(size_t) override { return 0; }
    bool read(size_t, char*, size_t) override { return false; }
    bool commit(size_t, char const*, size_t) override { return false; }
    bool remove(size_t) override { return false; }
} files;
static Store store(files);
static std::mutex storeMutex;
Store& runtimeStore() { return store; }
std::mutex& runtimeStoreMutex() { return storeMutex; }
bool Store::get(char const*, Profile& output, bool& builtIn, char const*& error)
{
    output = selectedProfile;
    builtIn = false;
    error = nullptr;
    return true;
}
} // namespace Hems::Profiles

namespace Hems::Modbus {
Result Client::open(char const* host, uint16_t port, uint8_t unit)
{
    wireHost = host; wirePort = port; wireUnit = unit;
    return {};
}
void Client::close() {}
Result Client::writeHoldingRegisters(uint16_t address, uint16_t count, uint16_t const* values)
{
    writes.push_back({ wireHost, wirePort, wireUnit, address });
    for (uint16_t i = 0; i < count; ++i) { registers[address + i] = values[i]; }
    if (onWrite) { auto callback = std::move(onWrite); onWrite = {}; callback(); }
    if (writeFails && delayedFailure) { testNow += Actuation::CleanupRetryMs + 1000; }
    return writeFails ? Result { Error::Timeout } : Result {};
}
Result Client::readHoldingRegisters(uint16_t address, uint16_t count, uint8_t* payload)
{
    for (uint16_t i = 0; i < count; ++i) {
        auto value = registers[address + i];
        payload[i * 2] = value >> 8;
        payload[i * 2 + 1] = value;
    }
    if (address == 90 && onIdentityRead) { auto callback = std::move(onIdentityRead); onIdentityRead = {}; callback(); }
    return { Error::None, 0, true };
}
Result Client::readInputRegisters(uint16_t address, uint16_t count, uint8_t* payload)
{
    return readHoldingRegisters(address, count, payload);
}
Result Client::readDiscreteInputs(uint16_t, uint16_t, uint8_t* payload)
{
    *payload = 0;
    return { Error::None, 0, true };
}
} // namespace Hems::Modbus

static std::function<void()> onAuthorization;
static int authorizationChecks;
static int reconfigureAt = -1;
static bool authorized(void*, ControlTarget)
{
    if (++authorizationChecks == reconfigureAt && onAuthorization) { onAuthorization(); }
    return true;
}

// Both provider implementations must keep old identity and recover after a
// failed acquire/operation even when config changes inside the remote write.
template<typename Provider, typename Configure>
static void lifecycle(Provider& provider, ControlTarget target, Configure configure)
{
    tasks.clear();
    writes.clear();
    registers.clear();
    testNow = 0;
    writeFails = false;
    NetworkSettings.connected = true;
    provider.setAuthorization(nullptr, authorized);
    configure(false);
    Actuation::Command command { target, Actuation::Action::Stop };
    uint32_t id = 0;
    assert(provider.submitCommand(command, id));
    configure(true); // Pending commands must be cancelled, never retargeted.
    runWorker();
    assert(writes.empty());
    assert(provider.commandSnapshot().state == Actuation::MailboxState::Cancelled);

    // Reconfiguration in the final authorization callback must revoke the
    // approval before the transport can send the ownership-producing write.
    configure(false);
    authorizationChecks = 0;
    reconfigureAt = 5; // submit, dispatch, open, executor gate, write gate
    onAuthorization = [&] { configure(true); };
    assert(provider.submitCommand(command, id));
    runWorker();
    assert(writes.empty());
    reconfigureAt = -1;
    onAuthorization = {};
    // Conservatively retained context is releasable even after a revoked write.
    provider.loop();
    runWorker();
    assert(!provider.cleanupSnapshot().retainedContext);
    writes.clear();

    configure(false);
    assert(provider.submitCommand(command, id));
    writeFails = true;
    onWrite = [&] { configure(true); };
    runWorker();
    assert(writes.size() == 1 && writes.back().host == "old.invalid");
    auto cleanup = provider.cleanupSnapshot();
    assert(cleanup.retainedContext && cleanup.state == Actuation::CleanupState::Pending);
    assert(provider.commandSnapshot().state == Actuation::MailboxState::Failed);
    assert(!provider.submitCommand(command, id));

    delayedFailure = true;
    provider.loop();
    runWorker();
    delayedFailure = false;
    auto failureTime = testNow;
    cleanup = provider.cleanupSnapshot();
    assert(cleanup.retainedContext && cleanup.attempts == 1);
    assert(cleanup.state == Actuation::CleanupState::RetryWait);
    assert(cleanup.lastResult.stage == Actuation::CommandStage::Release);
    assert(writes.back().host == "old.invalid");
    assert(writes.back().port == 1502 && writes.back().unit == 7);
    size_t before = writes.size();
    configure(true); // Must not reset/retry or replace the retained context.
    testNow = failureTime + Actuation::CleanupRetryMs - 1;
    provider.loop();
    runWorker();
    assert(writes.size() == before && provider.cleanupSnapshot().attempts == 1);
    testNow = failureTime + Actuation::CleanupRetryMs;
    writeFails = false;
    provider.loop();
    runWorker();
    cleanup = provider.cleanupSnapshot();
    assert(!cleanup.retainedContext && cleanup.attempts == 2);
    assert(cleanup.state == Actuation::CleanupState::Succeeded);
    assert(writes.back().host == "old.invalid" && writes.back().address == 12);
    assert(writes.back().port == 1502 && writes.back().unit == 7);
    assert(provider.submitCommand(command, id));
    runWorker();
    assert(provider.commandSnapshot().state == Actuation::MailboxState::Succeeded);
    assert(writes.back().host == "new.invalid" && writes.back().address == 21);
}

// Actual provider reboot dispatch: both targets use one existing worker/client,
// reject normal commands before loading, and never release a replacement config.
template<typename Provider, typename Configure>
static void rebootLifecycle(ControlTarget target, Configure configure, Profiles::Profile profile)
{
    tasks.clear(); writes.clear(); registers.clear();
    writeFails = false; NetworkSettings.connected = true;
    profile.identityFieldCount = 0; profile.releaseReplaySafe = false;
    selectedProfile = profile;
    MemoryJournalFiles files;
    Provider live;
    live.setAuthorization(nullptr, authorized);
    configure(live, false);
    Actuation::Command command { target, Actuation::Action::Stop };
    uint32_t id;
    assert(!live.submitCommand(command, id)); // NotLoaded always fails closed.
    live.initRecovery(files);
    assert(live.submitCommand(command, id)); runWorker();
    assert(live.recoverySnapshot().journalPresent);
    auto before = writes.size();
    Provider rebooted; rebooted.initRecovery(files); rebooted.setAuthorization(nullptr, authorized);
    configure(rebooted, false);
    assert(!rebooted.submitCommand(command, id));
    rebooted.loop(); runWorker();
    assert(rebooted.recoverySnapshot().state == Recovery::State::Unresolved);
    assert(rebooted.recoverySnapshot().reason == Recovery::Reason::IdentityUnsupported);
    configure(rebooted, true); rebooted.loop(); runWorker();
    assert(writes.size() == before && !rebooted.submitCommand(command, id));

    // Synthetic identity and replay-safe semantics, with a new separate journal.
    tasks.clear(); writes.clear();
    MemoryJournalFiles verifiedFiles;
    profile.identityFieldCount = 1; profile.identityFields[0] = { Modbus::ReadFunction::InputRegisters, 90, 2 };
    strcpy(profile.identitySource, "Synthetic provider identity fixture"); profile.releaseReplaySafe = true;
    selectedProfile = profile; registers[90] = 0x1234; registers[91] = 0x5678;
    Provider verifiedLive; verifiedLive.initRecovery(verifiedFiles); verifiedLive.setAuthorization(nullptr, authorized);
    configure(verifiedLive, false);
    assert(verifiedLive.submitCommand(command, id)); runWorker();
    auto armedFiles = verifiedFiles;
    before = writes.size();
    Provider verifiedBoot; verifiedBoot.initRecovery(verifiedFiles); configure(verifiedBoot, false);
    verifiedBoot.loop(); runWorker();
    assert(verifiedBoot.recoverySnapshot().state == Recovery::State::Recovered);
    assert(writes.size() == before + 1 && writes.back().host == "old.invalid" && writes.back().address == 12);

    Provider changedBoot; changedBoot.initRecovery(armedFiles); configure(changedBoot, false);
    onIdentityRead = [&] { configure(changedBoot, true); };
    before = writes.size(); changedBoot.loop(); runWorker();
    assert(writes.size() == before && changedBoot.recoverySnapshot().blocked);
    assert(changedBoot.recoverySnapshot().reason == Recovery::Reason::ConfigurationChanged);

    // Live retained cleanup + failed durable clear retries only persistence;
    // an unvalidated/non-replay-safe release is never repeated due to flash I/O.
    tasks.clear(); writes.clear();
    MemoryJournalFiles clearFiles;
    Provider clearLive; clearLive.initRecovery(clearFiles); clearLive.setAuthorization(nullptr, authorized);
    configure(clearLive, false); assert(clearLive.submitCommand(command, id)); runWorker();
    configure(clearLive, true); clearFiles.fail = true;
    clearLive.loop(); runWorker(); before = writes.size();
    assert(clearLive.recoverySnapshot().clearPending && clearLive.recoverySnapshot().blocked);
    assert(clearLive.cleanupSnapshot().retainedContext);
    testNow += Recovery::RetryMs; clearFiles.fail = false;
    clearLive.loop(); runWorker();
    assert(writes.size() == before && !clearLive.recoverySnapshot().blocked);
    assert(!clearLive.cleanupSnapshot().retainedContext);

    // Expiry/revocation while an ownership write is running must retain exact
    // context at completion, then release via the existing cleanup path.
    assert(clearLive.resumeCommands());
    configure(clearLive, false);
    assert(clearLive.submitCommand(command, id));
    onWrite = [&] { clearLive.requestRelease(); };
    runWorker();
    assert(clearLive.cleanupSnapshot().retainedContext);
    assert(!clearLive.submitCommand(command, id));
    clearLive.loop(); runWorker();
    assert(!clearLive.cleanupSnapshot().retainedContext && !clearLive.recoverySnapshot().journalPresent);
    assert(!clearLive.submitCommand(command, id)); // new session must explicitly resume
    assert(clearLive.resumeCommands());
}

int main()
{
    MemoryJournalFiles recoveryFiles;
    std::strcpy(selectedProfile.id, "test.synthetic");
    selectedProfile.schemaVersion = 2;
    selectedProfile.capabilities = Profiles::capabilityBit(Profiles::Capability::Storage);
    selectedProfile.port = 1502;
    selectedProfile.unit = 7;
    selectedProfile.entryCount = 1;
    std::strcpy(selectedProfile.entries[0].key, "soc_pct");
    selectedProfile.controlOperationCount = 1;
    selectedProfile.controlWriteCount = 3;
    selectedProfile.acquireWriteCount = 1;
    selectedProfile.releaseFirstWrite = 1;
    selectedProfile.releaseWriteCount = 1;
    selectedProfile.controlWrites[0].address = 10;
    selectedProfile.controlWrites[1].address = 12;
    selectedProfile.controlWrites[2].address = 11;
    auto& operation = selectedProfile.controlOperations[0];
    std::strcpy(operation.key, "stop");
    operation.firstWrite = 2;
    operation.writeCount = 1;
    auto old = selectedProfile;
    auto replacement = old;
    replacement.controlWrites[1].address = 22;
    replacement.controlWrites[2].address = 21;
    Storage::Provider storage;
    storage.initRecovery(recoveryFiles);
    lifecycle(storage, ControlTarget::Storage, [&](bool changed) {
        selectedProfile = changed ? replacement : old;
        StorageConfig config;
        config.Enabled = true;
        std::strcpy(config.Host, changed ? "new.invalid" : "old.invalid");
        std::strcpy(config.Profile, "test.storage");
        storage.configure(config, true);
    });

    old.capabilities = Profiles::capabilityBit(Profiles::Capability::Evse);
    old.acquireWriteCount = 0; // WARP establishes control in the operation itself.
    std::strcpy(old.entries[0].key, "charging_state");
    std::strcpy(old.controlOperations[0].key, "stop_charging");
    replacement = old;
    replacement.controlWrites[1].address = 22;
    replacement.controlWrites[2].address = 21;
    Warp::Provider warp;
    warp.initRecovery(recoveryFiles);
    lifecycle(warp, ControlTarget::Evse, [&](bool changed) {
        selectedProfile = changed ? replacement : old;
        WarpConfig config;
        config.Enabled = true;
        config.Port = changed ? 2502 : 1502;
        std::strcpy(config.Host, changed ? "new.invalid" : "old.invalid");
        Evse::BindingConfig binding;
        std::strcpy(binding.Profile, "test.evse");
        warp.configure(config, binding, true);
    });
    auto configureStorage = [](Storage::Provider& provider, bool changed) {
        StorageConfig config; config.Enabled = true;
        strcpy(config.Host, changed ? "new.invalid" : "old.invalid"); strcpy(config.Profile, "test.synthetic");
        provider.configure(config, true);
    };
    auto storageProfile = selectedProfile;
    storageProfile.capabilities = Profiles::capabilityBit(Profiles::Capability::Storage);
    strcpy(storageProfile.entries[0].key, "soc_pct"); strcpy(storageProfile.controlOperations[0].key, "stop");
    storageProfile.controlWrites[1].address = 12;
    rebootLifecycle<Storage::Provider>(ControlTarget::Storage, configureStorage, storageProfile);
    auto configureWarp = [](Warp::Provider& provider, bool changed) {
        WarpConfig config; config.Enabled = true; config.Port = 1502;
        strcpy(config.Host, changed ? "new.invalid" : "old.invalid");
        Evse::BindingConfig binding; strcpy(binding.Profile, "test.synthetic");
        provider.configure(config, binding, true);
    };
    auto evseProfile = storageProfile;
    evseProfile.capabilities = Profiles::capabilityBit(Profiles::Capability::Evse);
    strcpy(evseProfile.entries[0].key, "charging_state"); strcpy(evseProfile.controlOperations[0].key, "stop_charging");
    rebootLifecycle<Warp::Provider>(ControlTarget::Evse, configureWarp, evseProfile);
    std::cout << "Provider retained cleanup and reboot recovery lifecycle tests passed\n";
}
