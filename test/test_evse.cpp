// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/evse/Poller.h>
#include <hems/warp/ProfileAdapter.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace Hems;

namespace {

void putU32(uint8_t* target, uint32_t value)
{
    target[0] = value >> 24;
    target[1] = value >> 16;
    target[2] = value >> 8;
    target[3] = value;
}

struct Reader {
    unsigned calls = 0;
    unsigned failAt = 0;
    uint32_t version = 5;
    uint32_t iec = 2;
    uint32_t chargingState = 3;
    uint32_t currentMa = 16000;

    Modbus::Result readInputRegisters(uint16_t address, uint16_t count, uint8_t* target)
    {
        ++calls;
        if (calls == failAt) { return { Modbus::Error::Timeout, 0, false }; }
        assert(count == 2);
        if (address == 0) { putU32(target, version); }
        else if (address == 1000) { putU32(target, iec); }
        else if (address == 1002) { putU32(target, chargingState); }
        else if (address == 1010) { putU32(target, currentMa); }
        else { assert(false); }
        return { Modbus::Error::None, 0, true };
    }

    Modbus::Result readHoldingRegisters(uint16_t, uint16_t, uint8_t*)
    {
        assert(false);
        return { Modbus::Error::InvalidData, 0, true };
    }

    Modbus::Result readDiscreteInputs(uint16_t, uint16_t, uint8_t*)
    {
        assert(false);
        return { Modbus::Error::InvalidData, 0, true };
    }
};

Profiles::Entry inputU32(char const* key, uint16_t address, double scale = 1)
{
    Profiles::Entry entry;
    std::strcpy(entry.key, key);
    entry.space = Modbus::ReadFunction::InputRegisters;
    entry.address = address;
    entry.count = 2;
    entry.type = Profiles::Type::Uint32;
    entry.scale = scale;
    return entry;
}

Profiles::Profile warpProfile()
{
    Profiles::Profile profile;
    profile.capabilities = Profiles::capabilityBit(Profiles::Capability::Evse);
    profile.entryCount = 5;
    profile.entries[0] = inputU32("register_table_version", 0);
    profile.entries[1] = inputU32("iec_state", 1000);
    profile.entries[2] = inputU32("charging_state", 1002);
    profile.entries[3] = inputU32("allowed_current_a", 1010, 0.001);
    profile.entries[4] = inputU32("uptime_s", 12); // ignored vendor diagnostic
    return profile;
}

void testProfileCompatibility()
{
    auto profile = warpProfile();
    assert(Evse::supportsProfile(profile));
    profile.capabilities = Profiles::capabilityBit(Profiles::Capability::Storage);
    assert(!Evse::supportsProfile(profile));
    profile.capabilities = Profiles::capabilityBit(Profiles::Capability::Evse);
    std::strcpy(profile.entries[0].key, "vendor_only");
    std::strcpy(profile.entries[1].key, "vendor_only_2");
    std::strcpy(profile.entries[2].key, "vendor_only_3");
    std::strcpy(profile.entries[3].key, "vendor_only_4");
    assert(!Evse::supportsProfile(profile));
}

void testWarpProfilePollAndAdapter()
{
    auto profile = warpProfile();
    Reader reader;
    Evse::Sample sample;
    auto result = Evse::poll(reader, profile, sample);
    assert(result);
    assert(reader.calls == 4); // uptime is not an operational semantic and is skipped
    assert(sample.recognizedValues == 4 && sample.validValues == 4);
    assert(sample.tableVersion.valid && sample.tableVersion.value == 5);
    assert(sample.iecState.valid && sample.iecState.value == 2);
    assert(sample.chargingState.valid && sample.chargingState.value == 3);
    assert(sample.allowedCurrentA.valid && std::abs(sample.allowedCurrentA.value - 16.0) < 1e-9);
    assert(!sample.powerW.valid && !sample.sessionEnergyKWh.valid);

    Warp::Sample warp;
    assert(Warp::fromProfileSample(sample, warp));
    assert(warp.tableVersion == 5 && warp.iecState == 2 && warp.chargingState == 3);
    assert(std::abs(warp.allowedCurrentA - 16.0f) < 1e-6f);
    assert(warp.vehicleKnown && warp.vehicleConnected);
    assert(warp.chargingKnown && warp.charging);
    assert(!warp.meterAvailable); // WARP3 Smart has no integrated meter
}

void testUnknownAndBoundaries()
{
    auto profile = warpProfile();
    Reader reader;
    reader.iec = 4;
    reader.chargingState = 4;
    reader.currentMa = 0;
    Evse::Sample sample;
    assert(Evse::poll(reader, profile, sample));
    Warp::Sample warp;
    assert(Warp::fromProfileSample(sample, warp));
    assert(!warp.vehicleKnown && !warp.chargingKnown);
    assert(warp.allowedCurrentA == 0);

    reader = {};
    reader.currentMa = 5000;
    assert(Evse::poll(reader, profile, sample));
    assert(!Warp::fromProfileSample(sample, warp));

    reader = {};
    reader.version = 6;
    assert(Evse::poll(reader, profile, sample));
    assert(!Warp::fromProfileSample(sample, warp));
}

void testBooleanSafetyAndFailures()
{
    Evse::Sample sample;
    Profiles::Value value;
    value.valid = true;
    value.type = Profiles::Type::Uint16;
    value.number = 2;
    assert(Evse::apply(sample, "charging", value));
    assert(sample.recognizedValues == 1 && sample.validValues == 0 && !sample.charging.valid);

    value.number = 0;
    assert(Evse::apply(sample, "charging", value));
    assert(sample.charging.valid && !sample.charging.value);

    auto profile = warpProfile();
    Reader reader;
    reader.failAt = 2;
    auto result = Evse::poll(reader, profile, sample);
    assert(result.error == Modbus::Error::Timeout && !result.responded && reader.calls == 2);
}

} // namespace

int main()
{
    testProfileCompatibility();
    testWarpProfilePollAndAdapter();
    testUnknownAndBoundaries();
    testBooleanSafetyAndFailures();
    std::cout << "Profile-driven EVSE telemetry tests passed\n";
}
