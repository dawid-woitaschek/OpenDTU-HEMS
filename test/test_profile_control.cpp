// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/Profile.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace Hems::Profiles;

namespace {
std::string validV2()
{
    return R"({"schema":"opendtu-hems-modbus-profile/v2","id":"test.control","capabilities":["storage"],"metadata":{"vendor":"Test","model":"Control","source":"unit-test","source_revision":"1"},"provenance":{"author":"OpenDTU-HEMS","license":"GPL-2.0-or-later"},"transport":{"protocol":"modbus_tcp","default_port":502,"unit_id":1,"address_base":0},"telemetry":[{"key":"power_w","space":"holding","address":10,"count":1,"type":"int16","unit":"W"}],"control":{"acquire":[{"address":100,"type":"uint16","value":21930}],"release":[{"address":100,"type":"uint16","value":21947}],"operations":[{"key":"set_power_w","input":{"unit":"W","min":0,"max":2500,"step":5},"writes":[{"address":101,"type":"uint16","value":"input"},{"address":102,"type":"uint16","value":1}]},{"key":"stop","writes":[{"address":102,"type":"uint16","value":0}]}]}})";
}

bool parses(std::string const& json, Profile& profile, char const*& error)
{
    error = nullptr;
    return parse(json.data(), json.size(), profile, error);
}

std::string replaceOnce(std::string value, std::string const& from, std::string const& to)
{
    auto pos = value.find(from);
    assert(pos != std::string::npos);
    value.replace(pos, from.size(), to);
    return value;
}
} // namespace

int main()
{
    Profile profile;
    char const* error = nullptr;
    auto json = validV2();
    assert(parses(json, profile, error));
    assert(profile.schemaVersion == 2);
    assert(hasControl(profile));
    assert(profile.acquireWriteCount == 1 && profile.releaseWriteCount == 1);
    assert(profile.controlOperationCount == 2 && profile.controlWriteCount == 5);

    auto const& setPower = profile.controlOperations[0];
    assert(!strcmp(setPower.key, "set_power_w"));
    assert(setPower.hasInput && setPower.minimum == 0 && setPower.maximum == 2500 && setPower.step == 5);
    assert(setPower.writeCount == 2);
    auto const& inputWrite = profile.controlWrites[setPower.firstWrite];
    assert(inputWrite.address == 101 && inputWrite.usesInput && inputWrite.type == Type::Uint16);

    BoundedAllocator allocator;
    JsonDocument document(&allocator);
    serialize(profile, document.to<JsonObject>());
    assert(!document.overflowed());
    std::string normalized;
    serializeJson(document, normalized);
    Profile roundtrip;
    assert(parses(normalized, roundtrip, error));
    assert(roundtrip.schemaVersion == 2 && roundtrip.controlOperationCount == 2);
    assert(!strcmp(roundtrip.controlOperations[0].key, "set_power_w"));

    auto v1WithControl = replaceOnce(json, "profile/v2", "profile/v1");
    Profile rejected = profile;
    assert(!parses(v1WithControl, rejected, error));
    assert(error && !strcmp(error, "Control metadata requires profile schema v2"));

    auto missingConsumer = replaceOnce(json, "\"value\":\"input\"", "\"value\":5");
    assert(!parses(missingConsumer, rejected, error));

    auto invalidRange = replaceOnce(json, "\"min\":0,\"max\":2500", "\"min\":2501,\"max\":2500");
    assert(!parses(invalidRange, rejected, error));

    auto inputInAcquire = replaceOnce(json, "\"value\":21930", "\"value\":\"input\"");
    assert(!parses(inputInAcquire, rejected, error));

    auto unsupportedWriteType = replaceOnce(json, "\"address\":101,\"type\":\"uint16\"", "\"address\":101,\"type\":\"uint32\"");
    assert(!parses(unsupportedWriteType, rejected, error));

    auto v1ReadOnly = R"({"schema":"opendtu-hems-modbus-profile/v1","id":"test.readonly","metadata":{"vendor":"Test","model":"Read only","source":"unit-test","source_revision":"1"},"provenance":{"author":"OpenDTU-HEMS","license":"GPL-2.0-or-later"},"transport":{"protocol":"modbus_tcp","default_port":502,"unit_id":1,"address_base":0},"telemetry":[{"key":"value","space":"holding","address":1,"count":1,"type":"uint16"}]})";
    Profile legacy;
    assert(parse(v1ReadOnly, strlen(v1ReadOnly), legacy, error));
    assert(legacy.schemaVersion == 1 && !hasControl(legacy));

    std::cout << "Profile schema v2 control tests passed\n";
}
