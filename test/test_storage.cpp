// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/storage/Poller.h>
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
        assert(count == 1 && address == 10);
        uint16_t raw = invalidSoc ? 1001 : 1000;
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

    Modbus::Result readDiscreteInputs(uint16_t, uint16_t, uint8_t*)
    {
        assert(false);
        return { Modbus::Error::Protocol, 0, true };
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
    // Example of device-native positive discharge converted to HEMS positive charge.
    profile.entries[1] = entry("ac_power_w", Modbus::ReadFunction::InputRegisters, 20,
            Profiles::Type::Int16, -1);
    profile.entries[2] = entry("vendor_extra", Modbus::ReadFunction::InputRegisters, 99);

    Reader reader;
    Storage::Sample sample;
    assert(Storage::poll(reader, profile, sample));
    assert(reader.calls == 2); // Unknown profile keys are not polled.
    assert(sample.recognizedValues == 2 && sample.validValues == 2);
    assert(sample.socPct.valid && sample.socPct.value == 100);
    assert(sample.acPowerW.valid && sample.acPowerW.value == -500);
    assert(!sample.dcPowerW.valid && !sample.batteryVoltageV.valid);

    reader.calls = 0;
    reader.zeroAc = true;
    assert(Storage::poll(reader, profile, sample));
    assert(sample.acPowerW.valid && sample.acPowerW.value == 0); // zero is not unknown

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

int main()
{
    assert(Storage::supportedKey("soc_pct"));
    assert(Storage::supportedKey("ac_power_w"));
    assert(Storage::supportedKey("dc_power_w"));
    assert(!Storage::supportedKey("manufacturer_magic"));
    testAcOnly();
    testDcOnly();
    testInvalidAndFailures();
    std::cout << "Generic storage semantic/poller tests passed\n";
}
