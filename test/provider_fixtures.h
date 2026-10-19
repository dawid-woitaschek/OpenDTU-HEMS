// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <cassert>
#include <fstream>
#include <string>

namespace ProviderFixtures {
inline JsonDocument load(char const* name)
{
    std::ifstream input(std::string("fixtures/providers/") + name);
    if (!input) { input.open(std::string("test/fixtures/providers/") + name); }
    assert(input);
    JsonDocument doc; assert(!deserializeJson(doc, input)); return doc;
}
} // namespace ProviderFixtures
