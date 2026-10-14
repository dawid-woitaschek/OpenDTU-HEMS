// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/Profile.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace Hems::Profiles;

namespace {
std::string readFile(char const* path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream && "catalog profile file must exist");
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

Entry const* findEntry(Profile const& profile, char const* key)
{
    for (size_t i = 0; i < profile.entryCount; ++i) {
        if (!strcmp(profile.entries[i].key, key)) {
            return &profile.entries[i];
        }
    }
    return nullptr;
}

ControlOperation const* findOperation(Profile const& profile, char const* key)
{
    for (size_t i = 0; i < profile.controlOperationCount; ++i) {
        if (!strcmp(profile.controlOperations[i].key, key)) {
            return &profile.controlOperations[i];
        }
    }
    return nullptr;
}

ControlWrite const& writeAt(Profile const& profile, ControlOperation const& operation, size_t index)
{
    assert(index < operation.writeCount);
    return profile.controlWrites[operation.firstWrite + index];
}

Profile loadProfile(char const* path)
{
    auto json = readFile(path);
    Profile profile;
    char const* error = nullptr;
    if (!parse(json.data(), json.size(), profile, error)) {
        std::cerr << path << ": " << (error ? error : "unknown parse error") << '\n';
        assert(false && "catalog profile must pass production parser");
    }
    return profile;
}
} // namespace

int main()
{
    auto marstek = loadProfile("../device-profiles/storage/Marstek-Venus-E-3.0.json");
    assert(!strcmp(marstek.id, "marstek.venus-e3.v148"));
    assert(marstek.schemaVersion == 2 && hasControl(marstek));
    assert(hasCapability(marstek, Capability::Storage));
    assert(!hasCapability(marstek, Capability::Evse));

    auto soc = findEntry(marstek, "soc_pct");
    assert(soc && soc->address == 34002 && soc->type == Type::Uint16);
    assert(std::abs(soc->scale - 0.01) < 1e-12);

    auto acPower = findEntry(marstek, "ac_power_w");
    assert(acPower && acPower->address == 30006 && acPower->type == Type::Int16);
    assert(std::abs(acPower->scale + 1.0) < 1e-12);

    auto batteryCurrent = findEntry(marstek, "battery_current_a");
    assert(batteryCurrent && batteryCurrent->address == 30101 && batteryCurrent->type == Type::Int16);
    assert(std::abs(batteryCurrent->scale - 0.01) < 1e-12);

    assert(marstek.acquireWriteCount == 1 && marstek.releaseWriteCount == 1);
    auto const& acquire = marstek.controlWrites[marstek.acquireFirstWrite];
    auto const& release = marstek.controlWrites[marstek.releaseFirstWrite];
    assert(acquire.address == 42000 && acquire.constant == 21930 && !acquire.usesInput);
    assert(release.address == 42000 && release.constant == 21947 && !release.usesInput);

    auto charge = findOperation(marstek, "charge_power_w");
    assert(charge && charge->hasInput && charge->minimum == 0 && charge->maximum == 2500 && charge->step == 5);
    assert(charge->writeCount == 2);
    assert(writeAt(marstek, *charge, 0).address == 42020 && writeAt(marstek, *charge, 0).usesInput);
    assert(writeAt(marstek, *charge, 1).address == 42010 && writeAt(marstek, *charge, 1).constant == 1);

    auto discharge = findOperation(marstek, "discharge_power_w");
    assert(discharge && discharge->hasInput && discharge->minimum == 0 && discharge->maximum == 2500 && discharge->step == 5);
    assert(discharge->writeCount == 2);
    assert(writeAt(marstek, *discharge, 0).address == 42021 && writeAt(marstek, *discharge, 0).usesInput);
    assert(writeAt(marstek, *discharge, 1).address == 42010 && writeAt(marstek, *discharge, 1).constant == 2);

    auto stop = findOperation(marstek, "stop");
    assert(stop && !stop->hasInput && stop->writeCount == 3);
    assert(writeAt(marstek, *stop, 0).address == 42010 && writeAt(marstek, *stop, 0).constant == 0);
    assert(writeAt(marstek, *stop, 1).address == 42020 && writeAt(marstek, *stop, 1).constant == 0);
    assert(writeAt(marstek, *stop, 2).address == 42021 && writeAt(marstek, *stop, 2).constant == 0);

    auto warp = loadProfile("../device-profiles/evse/Tinkerforge-WARP3-Smart.json");
    assert(!strcmp(warp.id, "tinkerforge.warp3-smart"));
    assert(warp.schemaVersion == 2 && hasControl(warp));
    assert(hasCapability(warp, Capability::Evse));
    assert(!hasCapability(warp, Capability::Storage));
    assert(warp.entryCount == 4);
    assert(findEntry(warp, "uptime_s") == nullptr);

    auto tableVersion = findEntry(warp, "register_table_version");
    assert(tableVersion && tableVersion->address == 0 && tableVersion->type == Type::Uint32);

    auto iecState = findEntry(warp, "iec_state");
    assert(iecState && iecState->address == 1000 && iecState->type == Type::Uint32);

    auto chargingState = findEntry(warp, "charging_state");
    assert(chargingState && chargingState->address == 1002 && chargingState->type == Type::Uint32);

    auto allowedCurrent = findEntry(warp, "allowed_current_a");
    assert(allowedCurrent && allowedCurrent->address == 1010 && allowedCurrent->type == Type::Uint32);
    assert(std::abs(allowedCurrent->scale - 0.001) < 1e-12);

    auto setCurrent = findOperation(warp, "set_current_a");
    assert(setCurrent && setCurrent->hasInput && setCurrent->minimum == 6 && setCurrent->maximum == 32);
    assert(setCurrent->step == 1 && setCurrent->writeCount == 1);
    auto const& currentWrite = writeAt(warp, *setCurrent, 0);
    assert(currentWrite.address == 1002 && currentWrite.usesInput && currentWrite.type == Type::Uint32);
    assert(std::abs(currentWrite.scale - 0.001) < 1e-12);

    auto stopCharging = findOperation(warp, "stop_charging");
    assert(stopCharging && !stopCharging->hasInput && stopCharging->writeCount == 1);
    auto const& stopWrite = writeAt(warp, *stopCharging, 0);
    assert(stopWrite.address == 1002 && stopWrite.type == Type::Uint32 && stopWrite.constant == 0);

    std::cout << "Repository device-profile catalog passed telemetry/control parser checks\n";
}
