// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/Store.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <array>
using namespace Hems::Profiles;
struct MemoryFiles : Files {
    std::array<std::string, MaxImported> slots;
    bool fail = false;
    size_t size(size_t i) override { return slots[i].size(); }
    bool read(size_t i, char* b, size_t n) override { memcpy(b, slots[i].data(), n); return true; }
    bool commit(size_t i, char const* b, size_t n) override { if (fail) { return false; } slots[i].assign(b, n); return true; }
    bool remove(size_t i) override { if (fail) { return false; } slots[i].clear(); return true; }
};
std::string replace(std::string s, std::string const& old, std::string const& value) {
    auto i = s.find(old); assert(i != std::string::npos); s.replace(i, old.size(), value); return s;
}
int main() {
    char const* error = nullptr; Profile p, roundtrip;
    std::string example = exampleJson();
    assert(parse(example.data(), example.size(), p, error));
    assert(p.entryCount == 1 && std::abs(p.entries[0].scale - .1) < 1e-7);
    assert(!hasCapability(p, Capability::Storage));
    assert(!hasCapability(p, Capability::Evse));
    auto storageCapable = replace(example, "\"id\":\"example.synthetic\",",
            "\"id\":\"example.synthetic\",\"capabilities\":[\"storage\"],");
    Profile storageProfile;
    assert(parse(storageCapable.data(), storageCapable.size(), storageProfile, error));
    assert(hasCapability(storageProfile, Capability::Storage));
    assert(!hasCapability(storageProfile, Capability::Evse));
    JsonDocument storageDocument; serialize(storageProfile, storageDocument.to<JsonObject>());
    assert(storageDocument["capabilities"].as<JsonArrayConst>().size() == 1);
    assert(!strcmp(storageDocument["capabilities"][0].as<char const*>(), "storage"));
    auto evseCapable = replace(example, "\"id\":\"example.synthetic\",",
            "\"id\":\"example.synthetic\",\"capabilities\":[\"evse\"],");
    Profile evseProfile;
    assert(parse(evseCapable.data(), evseCapable.size(), evseProfile, error));
    assert(!hasCapability(evseProfile, Capability::Storage));
    assert(hasCapability(evseProfile, Capability::Evse));
    JsonDocument evseDocument; serialize(evseProfile, evseDocument.to<JsonObject>());
    assert(evseDocument["capabilities"].as<JsonArrayConst>().size() == 1);
    assert(!strcmp(evseDocument["capabilities"][0].as<char const*>(), "evse"));
    auto combinedCapable = replace(example, "\"id\":\"example.synthetic\",",
            "\"id\":\"example.synthetic\",\"capabilities\":[\"storage\",\"evse\"],");
    Profile combinedProfile;
    assert(parse(combinedCapable.data(), combinedCapable.size(), combinedProfile, error));
    assert(hasCapability(combinedProfile, Capability::Storage));
    assert(hasCapability(combinedProfile, Capability::Evse));
    uint8_t fullSoc[] { 3, 0xe8 }, overSoc[] { 3, 0xe9 };
    assert(decode(p.entries[0], fullSoc, sizeof(fullSoc)).valid);
    assert(decode(p.entries[0], fullSoc, sizeof(fullSoc)).number == 100);
    assert(!decode(p.entries[0], overSoc, sizeof(overSoc)).valid);
    auto checkBad = [&](std::string const& s) { Profile old = p; assert(!parse(s.data(), s.size(), p, error)); assert(error); assert(!memcmp(&old, &p, sizeof(p))); };
    for (auto s : {
        replace(example, "\"id\":\"example.synthetic\",", ""),
        replace(example, "example.synthetic", "../escape"),
        replace(example, "example.synthetic", "UPPER"),
        replace(example, "example.synthetic", std::string(49, 'a')),
        replace(example, "Example only", std::string(97, 'a')),
        replace(example, "\"schema\":", "\"schema\":0,\"schema\":"),
        replace(example, "\"key\":\"soc_pct\"", "\"key\":\"a\",\"key\":\"b\""),
        replace(example, "\"count\":1", "\"count\":0"),
        replace(example, "\"count\":1", "\"count\":3"),
        replace(example, "\"count\":1", "\"count\":-1"),
        replace(example, "\"count\":1", "\"count\":1.5"),
        replace(example, "uint16", "uint64"),
        replace(example, "\"address\":0", "\"address\":65536"),
        replace(storageCapable, "\"storage\"", "\"hvac\""),
        replace(storageCapable, "[\"storage\"]", "[\"storage\",\"storage\"]"),
        replace(evseCapable, "[\"evse\"]", "[\"evse\",\"evse\"]"),
        replace(storageCapable, "[\"storage\"]", "[]"),
    }) {
        checkBad(s);
    }
    for (auto s : {
        replace(example, "\"default_port\":502", "\"default_port\":0"),
        replace(example, "\"default_port\":502", "\"default_port\":65536"),
        replace(example, "\"unit_id\":1", "\"unit_id\":248"),
        replace(example, "\"unit_id\":1", "\"unit_id\":0"),
        replace(example, "\"address_base\":0", "\"address_base\":2"),
        replace(example, "modbus_tcp", "rtu"),
        replace(example, "holding", "coil"),
        replace(example, "\"scale\":0.1", "\"scale\":0"),
        replace(example, "\"scale\":0.1", "\"scale\":1e999"),
        replace(example, "\"scale\":0.1", "\"scale\":\"NaN\""),
        replace(example, "\"scale\":0.1", "\"offset\":1e999"),
        replace(example, "\"min\":0", "\"min\":101"),
        replace(example, "\"min\":0", "\"min\":null"),
        replace(example, "\"min\":0", "\"invalid_raw\":65536"),
        replace(example, "\"schema\":", "\"control\":{},\"schema\":"),
        replace(example, "\"schema\":", "\"failsafe\":{\"type\":\"unknown\"},\"schema\":"),
        replace(example, "Example only", "Example\\u0020only"),
        example + "{}", std::string(MaxJson + 1, ' '),
        replace(example, "uint16", "int32"),
        replace(replace(replace(example, "uint16", "int32"), "\"count\":1", "\"count\":2"), "\"address\":0", "\"address\":65535")
    }) { checkBad(s); }
    auto minimal = replace(replace(example, "example.synthetic", "user.minimal"), ",\"scale\":0.1,\"unit\":\"percent\",\"min\":0,\"max\":100", "");
    assert(parse(minimal.data(), minimal.size(), roundtrip, error));
    auto baseOne = replace(replace(minimal, "\"address_base\":0", "\"address_base\":1"), "\"address\":0", "\"address\":1");
    assert(parse(baseOne.data(), baseOne.size(), roundtrip, error) && roundtrip.entries[0].address == 0);
    checkBad(replace(baseOne, "\"address\":1", "\"address\":0"));
    JsonDocument doc; serialize(p, doc.to<JsonObject>());
    std::string normalized; serializeJson(doc, normalized);
    Profile detached = p; JsonDocument detachedDocument; serialize(detached, detachedDocument.to<JsonObject>());
    for (auto* s : { detached.id, detached.vendor, detached.model, detached.source, detached.revision,
            detached.description, detached.author, detached.license, detached.entries[0].key, detached.entries[0].unit }) {
        memset(s, 'x', strlen(s));
    }
    std::string detachedJson; serializeJson(detachedDocument, detachedJson); assert(detachedJson == normalized);
    assert(parse(normalized.data(), normalized.size(), roundtrip, error));
    assert(!strcmp(roundtrip.id, p.id) && !strcmp(roundtrip.license, p.license));
    JsonDocument exported; serialize(roundtrip, exported.to<JsonObject>());
    std::string exportedJson; serializeJson(exported, exportedJson); assert(exportedJson == normalized);
    std::string storageNormalized; serializeJson(storageDocument, storageNormalized);
    assert(parse(storageNormalized.data(), storageNormalized.size(), roundtrip, error));
    assert(hasCapability(roundtrip, Capability::Storage));
    std::string evseNormalized; serializeJson(evseDocument, evseNormalized);
    assert(parse(evseNormalized.data(), evseNormalized.size(), roundtrip, error));
    assert(hasCapability(roundtrip, Capability::Evse));
    std::cout << "Synthetic normalized profile=" << normalized.size() << " bytes\n";
    auto entry = doc["telemetry"][0].as<JsonObject>();
    doc["telemetry"].as<JsonArray>().add(entry);
    std::string duplicate; serializeJson(doc, duplicate); checkBad(duplicate);
    doc["telemetry"].to<JsonArray>();
    for (size_t i = 0; i <= MaxEntries; ++i) {
        auto e = doc["telemetry"].as<JsonArray>().add<JsonObject>();
        e["key"] = "k" + std::to_string(i); e["space"] = "input"; e["address"] = i; e["count"] = 1; e["type"] = "uint16";
    }
    std::string many; serializeJson(doc, many); checkBad(many);
    doc["telemetry"].as<JsonArray>().remove(MaxEntries);
    many.clear(); serializeJson(doc, many); assert(parse(many.data(), many.size(), roundtrip, error));
    BoundedAllocator maxAllocator; JsonDocument maxDocument(&maxAllocator);
    serialize(roundtrip, maxDocument.to<JsonObject>());
    std::cout << "24-entry normalized JSON=" << measureJson(maxDocument) << " bytes; allocator peak=" << maxAllocator.peak << " bytes\n";
    // Representative storage: signed power, float limit, mode/fault, zero-based boundary.
    doc.clear(); serialize(p, doc.to<JsonObject>()); auto array = doc["telemetry"].as<JsonArray>();
    for (auto type : { "int16", "uint32", "int32", "float32" }) {
        auto e = array.add<JsonObject>(); e["key"] = type; e["space"] = "input"; e["address"] = 10;
        e["count"] = !strcmp(type, "int16") ? 1 : 2; e["type"] = type;
    }
    auto bit = array.add<JsonObject>(); bit["key"] = "fault"; bit["space"] = "discrete"; bit["address"] = 65535; bit["count"] = 1; bit["type"] = "bool";
    std::string storage; serializeJson(doc, storage); assert(parse(storage.data(), storage.size(), roundtrip, error));
    Entry e; uint8_t zero[4] {}, negative[4] { 0xff, 0xff, 0xff, 0xfe }, f[4] { 0x3f, 0xc0, 0, 0 };
    assert(decode(e, zero, 2).valid && decode(e, zero, 2).number == 0);
    e.type = Type::Int16; assert(decode(e, negative + 2, 2).number == -2);
    e.scale = 2; e.offset = 5; assert(decode(e, negative + 2, 2).number == 1);
    e.scale = 1; e.offset = 0; e.count = 2; e.type = Type::Uint32; assert(decode(e, negative, 4).number == 4294967294.0);
    e.type = Type::Int32; assert(decode(e, negative, 4).number == -2);
    e.type = Type::Float32; assert(decode(e, f, 4).number == 1.5);
    uint8_t low[4] { 0, 0, 0x3f, 0xc0 }, little[4] { 0xc0, 0x3f, 0, 0 }, both[4] { 0, 0, 0xc0, 0x3f };
    e.lowWordFirst = true; assert(decode(e, low, 4).number == 1.5);
    e.lowWordFirst = false; e.littleBytes = true; assert(decode(e, little, 4).number == 1.5);
    e.lowWordFirst = true; assert(decode(e, both, 4).number == 1.5);
    e.lowWordFirst = false; e.littleBytes = false; e.hasMaximum = true; e.maximum = 1; assert(!decode(e, f, 4).valid);
    e.hasMaximum = false; assert(!decode(e, f, 3).valid && !decode(e, nullptr, 4).valid);
    uint8_t nan[4] { 0x7f, 0xc0, 0, 0 }, inf[4] { 0x7f, 0x80, 0, 0 }; assert(!decode(e, nan, 4).valid && !decode(e, inf, 4).valid);
    e.type = Type::Uint16; e.count = 1; e.hasInvalidRaw = true; e.invalidRaw = 0; assert(!decode(e, zero, 2).valid);
    e.hasInvalidRaw = false; e.type = Type::Bool; e.space = Hems::Modbus::ReadFunction::DiscreteInputs;
    assert(decode(e, zero, 1).valid && !decode(e, zero, 1).boolean);
    // Storage transaction and identity behavior; corruption isolation, bounded slots.
    MemoryFiles files; Store store(files); bool builtin;
    auto imported = replace(example, "example.synthetic", "user.storage");
    assert(!store.import(example.data(), example.size(), nullptr, error));
    assert(store.import(imported.data(), imported.size(), nullptr, error));
    assert(!store.import(imported.data(), imported.size(), nullptr, error));
    auto old = files.slots;
    auto malformed = replace(imported, "uint16", "bad"); assert(!store.import(malformed.data(), malformed.size(), "user.storage", error)); assert(files.slots == old);
    files.fail = true; assert(!store.import(imported.data(), imported.size(), "user.storage", error)); assert(files.slots == old); files.fail = false;
    assert(!store.import(imported.data(), imported.size(), "other", error));
    assert(store.import(imported.data(), imported.size(), "user.storage", error));
    assert(store.get("user.storage", roundtrip, builtin, error) && !builtin);
    assert(!store.erase("example.synthetic", error));
    assert(store.erase("user.storage", error)); assert(!store.get("user.storage", roundtrip, builtin, error));
    files.slots[0] = "corrupt"; assert(Store::builtin(roundtrip, error)); assert(!store.load(0, roundtrip, error));
    files.slots[0] = std::string(MaxJson + 1, 'x'); assert(!store.load(0, roundtrip, error));
    for (size_t i = 0; i < MaxImported; ++i) {
        auto s = replace(imported, "user.storage", "user.storage" + std::to_string(i)); assert(store.import(s.data(), s.size(), nullptr, error));
    }
    assert(!store.import(imported.data(), imported.size(), nullptr, error));
    files.slots[1] = files.slots[0]; assert(!store.get("user.storage0", roundtrip, builtin, error));
    assert(!store.erase("user.storage0", error));
    assert(!store.import(imported.data(), imported.size(), "user.storage0", error));
    BoundedAllocator allocator; void* allocation = allocator.allocate(MaxJsonMemory); assert(allocation && !allocator.allocate(1)); allocator.deallocate(allocation); assert(!allocator.used);
    std::cout << "Profile schema/decoder/store tests passed; sizeof(Profile)=" << sizeof(Profile) << " sizeof(Entry)=" << sizeof(Entry) << "\n";
}
