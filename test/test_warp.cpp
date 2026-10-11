// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/modbus/Protocol.h>
#include <hems/warp/Poller.h>
#include <hems/warp/Settings.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace Hems;

static void put32(uint8_t* target, uint32_t value)
{
    for (int i = 0; i < 4; ++i) { target[i] = value >> (24 - 8 * i); }
}

struct Reader {
    uint32_t version = 5;
    uint8_t features = 3;
    uint8_t evse[24] {};
    uint8_t meter[16] {};
    unsigned calls = 0;
    unsigned failAt = 0;
    Modbus::Result failure { Modbus::Error::Timeout };

    Reader()
    {
        put32(evse, 2);
        put32(evse + 4, 3);
        put32(evse + 20, 16000);
        put32(meter, 0x45e7e000); // Independent fixed wire fixture: 7420 W.
        put32(meter + 12, 0x4089999a); // 4.3 kWh, IEEE-754 float32.
    }
    Modbus::Result readInputRegisters(uint16_t address, uint16_t count, uint8_t* target)
    {
        if (++calls == failAt) { return failure; }
        if (address == 0) { assert(count == 2); put32(target, version); }
        else if (address == 1000) { assert(count == 12); memcpy(target, evse, 24); }
        else { assert(address == 2002 && count == 8 && (features & 2)); memcpy(target, meter, 16); }
        return { Modbus::Error::None, 0, true };
    }
    Modbus::Result readDiscreteInputs(uint16_t address, uint16_t count, uint8_t* target)
    {
        assert(address == 0 && count == 2);
        if (++calls == failAt) { return failure; }
        *target = features;
        return { Modbus::Error::None, 0, true };
    }
};

static void testProtocol()
{
    uint8_t request[12];
    assert(Modbus::makeReadRequest(request, 0x1234, 1, Modbus::ReadFunction::InputRegisters, 1000, 12));
    uint8_t expected[] { 0x12, 0x34, 0, 0, 0, 6, 1, 4, 3, 0xe8, 0, 12 };
    assert(memcmp(request, expected, 12) == 0);
    assert(!Modbus::makeReadRequest(request, 1, 1, static_cast<Modbus::ReadFunction>(6), 0, 1));
    assert(!Modbus::makeReadRequest(request, 1, 1, Modbus::ReadFunction::InputRegisters, 65535, 2));
    assert(!Modbus::makeReadRequest(request, 1, 1, Modbus::ReadFunction::InputRegisters, 0, 0));
    assert(!Modbus::makeReadRequest(request, 1, 1, Modbus::ReadFunction::InputRegisters, 0, 17));
    uint8_t response[] { 0x12, 0x34, 0, 0, 0, 7, 1, 4, 4, 0x45, 0xe7, 0xe0, 0 };
    uint8_t payload[32] {};
    auto parse = [&](uint8_t const* frame, size_t size) {
        return Modbus::parseReadResponse(frame, size, 0x1234, 1, Modbus::ReadFunction::InputRegisters, 2, payload);
    };
    assert(parse(response, sizeof(response)));
    assert(Modbus::decodeFloat32(payload) == 7420.0f);
    for (size_t i = 0; i < sizeof(response); ++i) { assert(!parse(response, i)); }
    for (unsigned index : { 0, 1, 2, 3, 4, 5, 6, 7, 8 }) {
        uint8_t bad[sizeof(response)];
        memcpy(bad, response, sizeof(bad));
        bad[index] ^= 1;
        assert(parse(bad, sizeof(bad)).error == Modbus::Error::Protocol);
    }
    uint8_t exception[] { 0x12, 0x34, 0, 0, 0, 3, 1, 0x84, 2 };
    auto error = parse(exception, sizeof(exception));
    assert(error.error == Modbus::Error::Exception && error.exception == 2 && error.responded);
    uint8_t discrete[] { 0, 1, 0, 0, 0, 4, 1, 2, 1, 3 };
    assert(Modbus::parseReadResponse(discrete, sizeof(discrete), 1, 1,
            Modbus::ReadFunction::DiscreteInputs, 2, payload));
    assert(payload[0] == 3);
}

static void testDecoding()
{
    Reader reader;
    Warp::Sample sample;
    assert(Warp::poll(reader, sample));
    assert(sample.charging && sample.chargingKnown && sample.vehicleConnected && sample.vehicleKnown);
    assert(sample.allowedCurrentA == 16 && sample.powerW == 7420);
    assert(sample.sessionEnergyKWh > 4.299f && sample.sessionEnergyKWh < 4.301f);
    for (uint32_t state = 0; state < 4; ++state) {
        Reader fixture;
        fixture.features = 1; // Smart: no meter reads and no invented measurements.
        put32(fixture.evse, state == 0 ? 0 : 1);
        put32(fixture.evse + 4, state);
        assert(Warp::poll(fixture, sample));
        assert(fixture.calls == 3 && !sample.meterAvailable);
        assert(sample.vehicleKnown && sample.vehicleConnected == (state != 0));
        assert(sample.chargingKnown && sample.charging == (state == 3));
    }
    Reader zero;
    put32(zero.evse + 20, 0);
    put32(zero.meter, 0);
    put32(zero.meter + 12, 0);
    assert(Warp::poll(zero, sample) && sample.powerW == 0 && sample.allowedCurrentA == 0);
    Reader fault;
    put32(fault.evse, 4);
    put32(fault.evse + 4, 4);
    assert(Warp::poll(fault, sample) && !sample.vehicleKnown && !sample.chargingKnown);
    for (auto invalid : { 5u, 0xffffffffu }) {
        Reader fixture;
        put32(fixture.evse + 4, invalid);
        assert(Warp::poll(fixture, sample).error == Modbus::Error::InvalidData);
    }
    for (auto invalid : { 1u, 5999u, 32001u, 0xffffffffu }) {
        Reader fixture;
        put32(fixture.evse + 20, invalid);
        assert(Warp::poll(fixture, sample).error == Modbus::Error::InvalidData);
    }
    for (auto invalid : { 0x7fc00000u, 0x7f800000u, 0xbf800000u }) {
        for (size_t offset : { 0u, 12u }) {
            Reader fixture;
            put32(fixture.meter + offset, invalid);
            assert(Warp::poll(fixture, sample).error == Modbus::Error::InvalidData);
        }
    }
    for (uint32_t version : { 2u, 3u, 4u, 5u }) {
        Reader fixture;
        fixture.version = version;
        assert(Warp::poll(fixture, sample));
    }
    for (uint32_t version : { 0u, 1u, 6u, 0xffffffffu }) {
        Reader fixture;
        fixture.version = version;
        assert(Warp::poll(fixture, sample).error == Modbus::Error::UnsupportedTable && fixture.calls == 1);
    }
    Reader unavailable;
    unavailable.features = 0;
    assert(Warp::poll(unavailable, sample).error == Modbus::Error::NoEvse);
    for (unsigned step = 1; step <= 4; ++step) {
        Reader fixture;
        fixture.failAt = step;
        assert(Warp::poll(fixture, sample).error == Modbus::Error::Timeout && fixture.calls == step);
        fixture.calls = 0;
        fixture.failure = { Modbus::Error::Exception, 2, true };
        auto result = Warp::poll(fixture, sample);
        assert(result.error == Modbus::Error::Exception && result.exception == 2 && result.responded);
    }
}

static void testState()
{
    Warp::State state;
    state.reset(true, true);
    auto snapshot = state.snapshot(0);
    assert(!snapshot.valid && !snapshot.reachable && !snapshot.hasUpdate);
    Reader reader;
    Warp::Sample sample;
    auto result = Warp::poll(reader, sample);
    state.apply(sample, result, 0); // A valid update at uptime zero must work.
    snapshot = state.snapshot(14999);
    assert(snapshot.valid && !snapshot.stale && snapshot.hasUpdate);
    snapshot = state.snapshot(15000);
    assert(!snapshot.valid && snapshot.stale && !snapshot.reachable);
    state.apply(sample, result, 20000);
    for (auto error : { Modbus::Error::Timeout, Modbus::Error::DnsFailure, Modbus::Error::Connection,
            Modbus::Error::Protocol, Modbus::Error::Exception, Modbus::Error::InvalidData }) {
        bool responded = error == Modbus::Error::Protocol || error == Modbus::Error::Exception
            || error == Modbus::Error::InvalidData;
        state.apply({}, { error, 2, responded }, 21000);
        snapshot = state.snapshot(22000);
        assert(!snapshot.valid && snapshot.reachable == responded && snapshot.error == error);
        assert(snapshot.lastSuccessMs == 20000 && snapshot.ageMs == 2000);
        assert(snapshot.sample.charging && snapshot.sample.vehicleConnected && snapshot.sample.powerW == 7420);
    }
    state.apply(sample, result, 23000);
    snapshot = state.snapshot(23001);
    assert(snapshot.valid && snapshot.reachable && snapshot.error == Modbus::Error::None);
    assert(snapshot.successfulPolls == 3 && snapshot.failedPolls == 6);
    state.reset(true, false);
    state.apply(sample, result, 24000);
    assert(!state.snapshot(24000).valid && !state.snapshot(24000).hasUpdate);
    state.reset(true, true);
    state.apply(sample, result, 0xfffffff0u);
    assert(state.snapshot(0x10).ageMs == 32 && state.snapshot(0x10).valid);
}

static void testSettings()
{
    WarpConfig config;
    assert(!config.Enabled && config.Host[0] == 0 && config.Port == 502);
    for (auto host : { "192.168.1.2", "warp", "warp-abc.local", "wallbox.example.org" }) { assert(validWarpHost(host)); }
    for (auto host : { "", "http://warp", "warp:502", "warp/path", " warp", "warp ", "-warp", "warp-", "warp..local", ".warp", "warp." }) {
        assert(!validWarpHost(host));
    }
    assert(!validWarpHost(std::string(254, 'a').c_str()));
    assert(!validWarpHost((std::string(64, 'a') + ".local").c_str()));
}

int main()
{
    testProtocol();
    testDecoding();
    testState();
    testSettings();
    std::cout << "WARP protocol, decoding, freshness, failure and recovery tests passed\n";
}
