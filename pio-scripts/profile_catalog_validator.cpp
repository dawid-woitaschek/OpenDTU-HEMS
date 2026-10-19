// SPDX-License-Identifier: GPL-2.0-or-later
// Host tool: reuse the authoritative bounded firmware parser and normalization.
#include <hems/profiles/Profile.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
int main(int argc, char** argv)
{
    if (argc != 2) { return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) { return 2; }
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Hems::Profiles::Profile profile;
    char const* error = nullptr;
    if (!Hems::Profiles::parse(json.data(), json.size(), profile, error)) {
        std::cerr << argv[1] << ": " << (error ? error : "invalid profile") << '\n';
        return 1;
    }
    Hems::Profiles::BoundedAllocator allocator;
    JsonDocument normalized(&allocator);
    Hems::Profiles::serialize(profile, normalized.to<JsonObject>());
    if (normalized.overflowed() || measureJson(normalized) > Hems::Profiles::MaxJson) { return 1; }
    serializeJson(normalized, std::cout);
}
