// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Poller.h>
#include <hems/storage/Settings.h>
#include <hems/storage/State.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace Hems;

struct Reader {
    unsigned calls = 0;
    unsigned failAt = 0;
    bool zeroAc = false;
    bool invalidSoc = false;
    bool charging = true;
    uint16_t faultFlag = 0;

    Modbus::Result maybeFail()
    {
        ++calls;
        if (calls == failAt) { return { Modbus::Error::Timeout, 0, false }; }
        return { Modbus::Error::None, 0, true };
    }

    Modbus::Result readHoldingRegisters(uint16_t address, uint16_t count, uint8_t* target)
    {
        auto result = maybeFail();
        if (!result) { return result; }
        assert(count == 1);
        uint16_t raw = 0;
        if (address == 10) { raw = invalidSoc ? 1001 : 1000; }
        else if (address == 41) { raw = faultFlag; }
        else { assert(false); }
        target[0] = raw >> 8;
        target[1] = raw & 0xff;
        return result;
    }

    Modbus::Result readInputRegisters(uint16_t address, uint16_t count, uint8_t* target)
    {
        auto result = maybeFail();
        if (!result) { return result; }
        assert(count == 1);
        int16_t raw = 0;
        if (address == 20) { raw = zeroAc ? 0 : 500; }
        else if (address == 30) { raw = -100; }
        else if (address == 31) { raw = 512; }
        else if (address == 32) { raw = -50; }
        else { assert(false); }
        auto bits = static_cast<uint16_t>(raw);
        target[0] = bits >> 8;
        target[1] = bits & 0xff;
        return result;
    }

    Modbus::Result readDiscreteInputs(uint16_t address, uint16_t count, uint8_t* target)
    {
        auto result = maybeFail();
        if (!result) { return result; }
        assert(address == 40 && count == 1);
        target[0] = charging ? 1 : 0;
        return result;
    }
};

static Profiles::Entry entry(char const* key, Modbus::ReadFunction space, uint16_t address,
        Profiles::Type type = Profiles::Type::Int16, double scale = 1)
{
    Profiles::Entry result;
    std::strcpy(result.key, key);
    result.space = space;
    result.address = address;
    result.type = type;
    result.count = 1;
    result.scale = scale;
    return result;
}

static void testAcOnly()
{
    Profiles::Profile profile;
    profile.entryCount = 3;
    profile.entries[0] = entry("soc_pct", Modbus::ReadFunction::HoldingRegisters, 10,
            Profiles::Type::Uint16, 0.1);
    profile.entries[0].hasMinimum = true;
    profile.entries[0].minimum = 0;
    profile.entries[0].hasMaximum = true;
    profile.entries[0].maximum = 100;
    profile.entries[1] = entry("ac_power_w", Modbus::ReadFunction::InputRegisters, 20,
            Profiles::Type::Int16, -1);
    profile.entries[2] = entry("vendor_extra", Modbus::ReadFunction::InputRegisters, 99);

    Reader reader;
    Storage::Sample sample;
    assert(Storage::poll(reader, profile, sample));
    assert(reader.calls == 2);
    assert(sample.recognizedValues == 2 && sample.validValues == 2);
    assert(sample.socPct.valid && sample.socPct.value == 100);
    assert(sample.acPowerW.valid && sample.acPowerW.value == -500);
    assert(!sample.dcPowerW.valid && !sample.batteryVoltageV.valid);

    reader.calls = 0;
    reader.zeroAc = true;
    assert(Storage::poll(reader, profile, sample));
    assert(sample.acPowerW.valid && sample.acPowerW.value == 0);

    reader.calls = 0;
    reader.invalidSoc = true;
    assert(Storage::poll(reader, profile, sample));
    assert(!sample.socPct.valid && sample.acPowerW.valid && sample.validValues == 1);
}

static void testDcOnly()
{
    Profiles::Profile profile;
    profile.entryCount = 3;
    profile.entries[0] = entry("dc_power_w", Modbus::ReadFunction::InputRegisters, 30);
    profile.entries[1] = entry("battery_voltage_v", Modbus::ReadFunction::InputRegisters, 31,
            Profiles::Type::Uint16, 0.1);
    profile.entries[2] = entry("battery_current_a", Modbus::ReadFunction::InputRegisters, 32,
            Profiles::Type::Int16, 0.1);

    Reader reader;
    Storage::Sample sample;
    assert(Storage::poll(reader, profile, sample));
    assert(sample.dcPowerW.valid && sample.dcPowerW.value == -100);
    assert(sample.batteryVoltageV.valid && std::abs(sample.batteryVoltageV.value - 51.2) < 1e-9);
    assert(sample.batteryCurrentA.valid && sample.batteryCurrentA.value == -5);
    assert(!sample.acPowerW.valid && !sample.socPct.valid);
}

static void testDiscreteAndNumericFlags()
{
    Profiles::Profile profile;
    profile.entryCount = 2;
    profile.entries[0] = entry("charging", Modbus::ReadFunction::DiscreteInputs, 40, Profiles::Type::Bool);
    profile.entries[1] = entry("fault_active", Modbus::ReadFunction::HoldingRegisters, 41, Profiles::Type::Uint16);

    Reader reader;
    Storage::Sample sample;
    assert(Storage::poll(reader, profile, sample));
    assert(sample.charging.valid && sample.charging.value);
    assert(sample.faultActive.valid && !sample.faultActive.value);
    assert(sample.recognizedValues == 2 && sample.validValues == 2);

    reader.calls = 0;
    reader.charging = false;
    reader.faultFlag = 1;
    assert(Storage::poll(reader, profile, sample));
    assert(sample.charging.valid && !sample.charging.value);
    assert(sample.faultActive.valid && sample.faultActive.value);

    reader.calls = 0;
    reader.faultFlag = 2;
    assert(Storage::poll(reader, profile, sample));
    assert(sample.charging.valid && !sample.faultActive.valid);
    assert(sample.validValues == 1); // arbitrary enums are not coerced to true
}

static void testInvalidAndFailures()
{
    Profiles::Profile profile;
    profile.entryCount = 1;
    profile.entries[0] = entry("soc_pct", Modbus::ReadFunction::HoldingRegisters, 10,
            Profiles::Type::Uint16, 0.1);
    profile.entries[0].hasMaximum = true;
    profile.entries[0].maximum = 100;

    Reader reader;
    reader.invalidSoc = true;
    Storage::Sample sample;
    auto result = Storage::poll(reader, profile, sample);
    assert(result.error == Modbus::Error::InvalidData && result.responded);
    assert(sample.recognizedValues == 1 && sample.validValues == 0);

    Profiles::Profile unknown;
    unknown.entryCount = 1;
    unknown.entries[0] = entry("vendor_extra", Modbus::ReadFunction::InputRegisters, 99);
    reader.calls = 0;
    result = Storage::poll(reader, unknown, sample);
    assert(result.error == Modbus::Error::InvalidData && result.responded && reader.calls == 0);

    reader = {};
    reader.failAt = 1;
    result = Storage::poll(reader, profile, sample);
    assert(result.error == Modbus::Error::Timeout && !result.responded && reader.calls == 1);
}

static void testState()
{
    Storage::State state;
    state.reset(true, true, true, 502, 1);
    auto snapshot = state.snapshot(1000);
    assert(snapshot.configured && snapshot.enabled && snapshot.profileLoaded);
    assert(!snapshot.valid && !snapshot.reachable && snapshot.port == 502 && snapshot.unit == 1);

    Storage::Sample sample;
    sample.socPct = { true, 50 };
    sample.validValues = 1;
    sample.recognizedValues = 1;
    state.apply(sample, { Modbus::Error::None, 0, true }, 2000);
    snapshot = state.snapshot(2000);
    assert(snapshot.valid && snapshot.reachable && snapshot.hasUpdate);
    assert(snapshot.successfulPolls == 1 && snapshot.failedPolls == 0);
    assert(snapshot.sample.socPct.valid && snapshot.sample.socPct.value == 50);

    state.apply({}, { Modbus::Error::Timeout, 0, false }, 3000);
    snapshot = state.snapshot(3000);
    assert(!snapshot.valid && !snapshot.reachable);
    assert(snapshot.successfulPolls == 1 && snapshot.failedPolls == 1);
    assert(snapshot.sample.socPct.valid && snapshot.sample.socPct.value == 50);

    snapshot = state.snapshot(17000);
    assert(snapshot.stale && !snapshot.valid && !snapshot.reachable);
}

static void testSettings()
{
    assert(Modbus::validHost("storage.local"));
    assert(Modbus::validHost("10.0.1.20"));
    assert(!Modbus::validHost(""));
    assert(!Modbus::validHost("-storage.local"));
    assert(validStorageProfileId("vendor.model-v1"));
    assert(!validStorageProfileId("Vendor Model"));

    JsonDocument doc;
    auto object = doc.to<JsonObject>();
    object["enabled"] = true;
    object["host"] = "storage.local";
    object["profile"] = "vendor.model-v1";
    object["port"] = 0;
    object["unit"] = 0;
    StorageConfig config;
    assert(deserializeStorageConfig(doc.as<JsonVariantConst>(), config));
    assert(config.Enabled && !strcmp(config.Host, "storage.local") && !strcmp(config.Profile, "vendor.model-v1"));
    assert(config.Port == 0 && config.Unit == 0);

    JsonDocument roundTrip;
    serializeStorageConfig(config, roundTrip.to<JsonObject>());
    StorageConfig decoded;
    assert(deserializeStorageConfig(roundTrip.as<JsonVariantConst>(), decoded));
    assert(decoded.Enabled && !strcmp(decoded.Host, config.Host) && !strcmp(decoded.Profile, config.Profile));

    JsonDocument detached;
    serializeStorageConfig(config, detached.to<JsonObject>());
    std::strcpy(config.Host, "mutated.local");
    std::strcpy(config.Profile, "mutated.model");
    assert(!strcmp(detached["host"].as<char const*>(), "storage.local"));
    assert(!strcmp(detached["profile"].as<char const*>(), "vendor.model-v1"));

    object["unit"] = 248;
    assert(!deserializeStorageConfig(doc.as<JsonVariantConst>(), decoded));
    object["unit"] = 1;
    object["host"] = "http://storage";
    assert(!deserializeStorageConfig(doc.as<JsonVariantConst>(), decoded));
    object["host"] = "storage.local";
    object["profile"] = "Vendor Model";
    assert(!deserializeStorageConfig(doc.as<JsonVariantConst>(), decoded));

    doc.clear();
    object = doc.to<JsonObject>();
    object["enabled"] = false;
    object["host"] = "storage.local";
    object["profile"] = "vendor.model-v1";
    object["port"] = 0;
    object["unit"] = 0;
    assert(deserializeStorageConfig(doc.as<JsonVariantConst>(), decoded));
    assert(!decoded.Enabled && !strcmp(decoded.Host, "storage.local") && decoded.Profile[0] == '\0');
}

static void testProfileCapability()
{
    Profiles::Profile storage;
    storage.entryCount = 1;
    storage.entries[0] = entry("soc_pct", Modbus::ReadFunction::HoldingRegisters, 10, Profiles::Type::Uint16);
    assert(Storage::supportsProfile(storage));

    Profiles::Profile unrelated;
    unrelated.entryCount = 1;
    unrelated.entries[0] = entry("vehicle_state", Modbus::ReadFunction::HoldingRegisters, 10, Profiles::Type::Uint16);
    assert(!Storage::supportsProfile(unrelated));
}

int main()
{
    assert(Storage::supportedKey("soc_pct"));
    assert(Storage::supportedKey("ac_power_w"));
    assert(Storage::supportedKey("dc_power_w"));
    assert(Storage::supportedKey("charging"));
    assert(Storage::supportedKey("fault_active"));
    assert(!Storage::supportedKey("manufacturer_magic"));
    testAcOnly();
    testDcOnly();
    testDiscreteAndNumericFlags();
    testInvalidAndFailures();
    testState();
    testSettings();
    testProfileCapability();
    std::cout << "Generic storage semantic/poller/state/settings tests passed\n";
}
