// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <hems/modbus/Protocol.h>

namespace Hems::Profiles {
inline constexpr char SchemaV1[] = "opendtu-hems-modbus-profile/v1";
inline constexpr char SchemaV2[] = "opendtu-hems-modbus-profile/v2";
constexpr size_t MaxJson = 6144;
constexpr size_t MaxJsonMemory = 12288;
constexpr size_t MaxEntries = 24;
constexpr size_t MaxControlOperations = 4;
constexpr size_t MaxControlWrites = 12;
constexpr size_t MaxImported = 4;
constexpr size_t MaxId = 48;
constexpr size_t MaxText = 96;
constexpr size_t MaxKey = 32;

enum class Type : uint8_t { Bool, Uint16, Int16, Uint32, Int32, Float32 };
enum class Capability : uint8_t { Storage = 1, Evse = 2 };
inline constexpr uint8_t capabilityBit(Capability capability) { return static_cast<uint8_t>(capability); }
struct Entry {
    char key[MaxKey + 1] {};
    char unit[17] {};
    Modbus::ReadFunction space = Modbus::ReadFunction::InputRegisters;
    Type type = Type::Uint16;
    uint16_t address = 0; // normalized zero-based wire address
    uint8_t count = 1;
    bool littleBytes = false;
    bool lowWordFirst = false;
    double scale = 1, offset = 0, minimum = 0, maximum = 0;
    bool hasMinimum = false, hasMaximum = false, hasInvalidRaw = false;
    uint32_t invalidRaw = 0;
};
struct ControlWrite {
    uint16_t address = 0; // normalized zero-based holding-register address
    int64_t constant = 0;
    double scale = 1, offset = 0; // input = raw * scale + offset
    Type type = Type::Uint16;
    bool usesInput = false;
};
struct ControlOperation {
    char key[MaxKey + 1] {};
    char unit[17] {};
    double minimum = 0, maximum = 0, step = 0;
    uint8_t firstWrite = 0, writeCount = 0;
    bool hasInput = false;
};
struct Profile {
    char id[MaxId + 1] {};
    char vendor[MaxText + 1] {}, model[MaxText + 1] {}, source[MaxText + 1] {};
    char revision[MaxText + 1] {}, description[MaxText + 1] {};
    char author[MaxText + 1] {}, license[MaxText + 1] {};
    uint16_t port = 502;
    uint8_t unit = 1, addressBase = 0, entryCount = 0, capabilities = 0, schemaVersion = 1;
    uint8_t controlOperationCount = 0, controlWriteCount = 0;
    uint8_t acquireFirstWrite = 0, acquireWriteCount = 0, releaseFirstWrite = 0, releaseWriteCount = 0;
    Entry entries[MaxEntries] {};
    ControlOperation controlOperations[MaxControlOperations] {};
    ControlWrite controlWrites[MaxControlWrites] {};
};
inline bool hasCapability(Profile const& profile, Capability capability)
{
    return (profile.capabilities & capabilityBit(capability)) != 0;
}
inline bool hasControl(Profile const& profile)
{
    return profile.controlOperationCount != 0 || profile.acquireWriteCount != 0 || profile.releaseWriteCount != 0;
}
static_assert(sizeof(Profile) <= 4096, "Profile model must fit the no-PSRAM transient budget");
struct Value { bool valid = false; bool boolean = false; double number = 0; Type type = Type::Uint16; };
// Allocations are capped including ArduinoJson string allocations and reallocation.
class BoundedAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override;
    void deallocate(void* pointer) override;
    void* reallocate(void* pointer, size_t size) override;
    size_t used = 0, peak = 0;
};
bool parse(char const* json, size_t size, Profile& output, char const*& error);
void serialize(Profile const& profile, JsonObject root);
Value decode(Entry const& entry, uint8_t const* payload, size_t size);
char const* typeName(Type type);
char const* spaceName(Modbus::ReadFunction space);
char const* schemaName(Profile const& profile);
bool validId(char const* id);
char const* exampleJson();
} // namespace Hems::Profiles