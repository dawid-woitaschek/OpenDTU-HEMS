// SPDX-License-Identifier: GPL-2.0-or-later
// Shared host-only runtime seams. Production policy/provider/semantic control are unchanged.
#pragma once
#include <hems/Controller.h>
#include <hems/model/Sources.h>
#include <hems/evse/SettingsStore.h>
#include <hems/storage/SettingsStore.h>
#include <fstream>
#include <sstream>
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
static std::function<void()> observeBoundary;
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
    if (observeBoundary) { observeBoundary(); }
    for (auto& task : tasks) {
        if (!task->notified) { continue; }
        currentTask = task.get();
        try { task->entry(task->context); } catch (WorkerIdle const&) {}
        if (observeBoundary) { observeBoundary(); }
    }
}

using namespace Hems;
static std::map<std::string, Profiles::Profile> selectedProfiles;
static std::function<void()> onWrite;
struct Write { std::string host; uint16_t port; uint8_t unit; uint16_t address; uint16_t value; };
static std::vector<Write> writes;
static std::string wireHost;
static uint16_t wirePort;
static uint8_t wireUnit;
static std::map<uint16_t, uint16_t> registers, inputRegisters;
static bool writeFails;
static bool readFails;
static uint16_t faultReadbackAddress = 0;
static bool readbackFails = false, readbackMismatch = false;

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
bool Store::get(char const* id, Profile& output, bool& builtIn, char const*& error)
{
    output = selectedProfiles.at(id);
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
    writes.push_back({ wireHost, wirePort, wireUnit, address, values[0] });
    for (uint16_t i = 0; i < count; ++i) { registers[address + i] = values[i]; }
    if (onWrite) { auto callback = std::move(onWrite); onWrite = {}; callback(); }
    return writeFails ? Result { Error::Timeout } : Result {};
}
Result Client::readHoldingRegisters(uint16_t address, uint16_t count, uint8_t* payload)
{
    if (readFails || (readbackFails && address == faultReadbackAddress)) { return { Error::Timeout }; }
    for (uint16_t i = 0; i < count; ++i) {
        auto value = registers[address + i];
        if (readbackMismatch && address == faultReadbackAddress) { value ^= 1; }
        payload[i * 2] = value >> 8;
        payload[i * 2 + 1] = value;
    }
    return { Error::None, 0, true };
}
Result Client::readInputRegisters(uint16_t address, uint16_t count, uint8_t* payload)
{
    if (readFails) { return { Error::Timeout }; }
    for (uint16_t i = 0; i < count; ++i) {
        auto value = inputRegisters[address + i]; payload[i * 2] = value >> 8; payload[i * 2 + 1] = value;
    }
    return { Error::None, 0, true };
}
Result Client::readDiscreteInputs(uint16_t, uint16_t, uint8_t* payload)
{
    *payload = 0;
    return { Error::None, 0, true };
}
} // namespace Hems::Modbus



namespace Hems::Storage {
SettingsStore StorageSettings;
void SettingsStore::init() {}
StorageConfig SettingsStore::get() const { return _config; }
bool SettingsStore::set(StorageConfig const& value) { _config = value; return true; }
}
namespace Hems::Evse {
SettingsStore BindingSettings;
void SettingsStore::init() {}
BindingConfig SettingsStore::get() const { return _config; }
bool SettingsStore::set(BindingConfig const& value) { _config = value; return true; }
}
namespace Hems::Model { void normalizeSources(SystemState&) {} }

static Profiles::Profile load(char const* path)
{
    std::ifstream file(path); std::stringstream content; content << file.rdbuf();
    auto json = content.str(); Profiles::Profile profile; char const* error = nullptr;
    assert(Profiles::parse(json.c_str(), json.size(), profile, error));
    return profile;
}
static void freshRegisters()
{
    inputRegisters[1] = 2; // uint32 WARP table version
    inputRegisters[1001] = 2; inputRegisters[1003] = 3; // connected and charging
    inputRegisters[1011] = 6000;
    registers[34002] = 500; // storage 50% SoC
}
