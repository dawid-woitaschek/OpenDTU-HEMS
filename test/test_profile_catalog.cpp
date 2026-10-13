// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/Profile.h>

#include <cassert>
#include <cmath>
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

    auto warp = loadProfile("../device-profiles/evse/Tinkerforge-WARP3-Smart.json");
    assert(!strcmp(warp.id, "tinkerforge.warp3-smart"));
    assert(hasCapability(warp, Capability::Evse));
    assert(!hasCapability(warp, Capability::Storage));

    auto chargingState = findEntry(warp, "charging_state");
    assert(chargingState && chargingState->address == 1002 && chargingState->type == Type::Uint32);

    auto allowedCurrent = findEntry(warp, "allowed_current_a");
    assert(allowedCurrent && allowedCurrent->address == 1010 && allowedCurrent->type == Type::Uint32);
    assert(std::abs(allowedCurrent->scale - 0.001) < 1e-12);

    std::cout << "Repository device-profile catalog passed production parser checks\n";
}
