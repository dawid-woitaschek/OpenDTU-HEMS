// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/Profile.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace Hems::Profiles {
namespace {
// A lexical gate rejects duplicate object keys, trailing input, escapes and
// non-ASCII strings before ArduinoJson allocates. v1 deliberately uses printable
// ASCII metadata. Fixed key lists and depth bound also bound scanner stack use.
class Gate {
public:
    Gate(char const* text, size_t size) : p(text), end(text + size) {}
    bool run() { return value(0) && (ws(), p == end); }
private:
    char const* p;
    char const* end;
    void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) { ++p; } }
    bool take(char c) { ws(); if (p == end || *p != c) { return false; } ++p; return true; }
    bool string(char const*& start, size_t& length) {
        if (!take('"')) { return false; }
        start = p;
        while (p < end && *p != '"') {
            if (*p < 32 || *p > 126 || *p == '\\') { return false; }
            ++p;
        }
        length = p - start;
        return p < end && (++p, true);
    }
    bool value(unsigned depth) {
        ws(); if (p == end || depth > 4) { return false; }
        if (*p == '"') { char const* start; size_t length; return string(start, length); }
        if (*p == '{') {
            ++p; ws(); if (take('}')) { return true; }
            char const* keys[16]; size_t lengths[16]; size_t count = 0;
            do {
                if (count == 16 || !string(keys[count], lengths[count]) || lengths[count] > 32) { return false; }
                for (size_t i = 0; i < count; ++i) {
                    if (lengths[i] == lengths[count] && !memcmp(keys[i], keys[count], lengths[i])) { return false; }
                }
                ++count;
                if (!take(':') || !value(depth + 1)) { return false; }
            } while (take(','));
            return take('}');
        }
        if (*p == '[') {
            ++p; if (take(']')) { return true; }
            size_t count = 0;
            do { if (++count > MaxEntries || !value(depth + 1)) { return false; } } while (take(','));
            return take(']');
        }
        for (auto literal : { "true", "false", "null" }) {
            size_t n = strlen(literal);
            if (size_t(end - p) >= n && !memcmp(p, literal, n)) { p += n; return true; }
        }
        if (*p == '-') { ++p; }
        if (p == end || *p < '0' || *p > '9') { return false; }
        if (*p == '0') { ++p; } else { while (p < end && *p >= '0' && *p <= '9') { ++p; } }
        if (p < end && *p == '.') {
            ++p; auto start = p;
            while (p < end && *p >= '0' && *p <= '9') { ++p; }
            if (p == start) { return false; }
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p; if (p < end && (*p == '+' || *p == '-')) { ++p; }
            auto start = p; while (p < end && *p >= '0' && *p <= '9') { ++p; }
            if (p == start) { return false; }
        }
        return true;
    }
};
bool fields(JsonObjectConst obj, std::initializer_list<char const*> allowed) {
    if (obj.isNull()) { return false; }
    for (auto field : obj) {
        bool known = false;
        for (auto key : allowed) { if (!strcmp(field.key().c_str(), key)) { known = true; break; } }
        if (!known) { return false; }
    }
    return true;
}
bool text(JsonVariantConst value, char* dest, size_t capacity, bool optional = false) {
    if (optional && value.isNull()) { return true; }
    if (!value.is<char const*>()) { return false; }
    auto s = value.as<char const*>(); auto n = strlen(s);
    if (n == 0 || n >= capacity) { return false; }
    memcpy(dest, s, n + 1); return true;
}
bool numeric(JsonVariantConst value, double& out) {
    if (!value.is<double>() || value.is<bool>()) { return false; }
    if (!std::isfinite(value.as<double>()) || std::abs(value.as<double>()) > 1e12) { return false; }
    // ArduinoJson stores short decimals as float. Use its canonical decimal
    // representation, then convert to double, rather than promoting binary float
    // noise (e.g. 0.1 -> 0.10000000149 and rejecting a legitimate 100% boundary).
    char decimal[48];
    size_t length = serializeJson(value, decimal, sizeof(decimal));
    if (length == 0 || length >= sizeof(decimal)) { return false; }
    char* end = nullptr;
    out = strtod(decimal, &end);
    return end == decimal + length && std::isfinite(out) && std::abs(out) <= 1e12;
}
bool equals(JsonVariantConst value, char const* expected) {
    return value.is<char const*>() && !strcmp(value.as<char const*>(), expected);
}
bool parseCapabilities(JsonVariantConst value, Profile& profile) {
    if (value.isUnbound()) { return true; }
    auto values = value.as<JsonArrayConst>();
    if (values.isNull() || values.size() == 0) { return false; }
    for (auto item : values) {
        uint8_t bit = 0;
        if (equals(item, "storage")) { bit = capabilityBit(Capability::Storage); }
        else { return false; }
        if (profile.capabilities & bit) { return false; }
        profile.capabilities |= bit;
    }
    return true;
}
struct alignas(std::max_align_t) Allocation { size_t size; };
} // namespace

void* BoundedAllocator::allocate(size_t size) {
    if (size > MaxJsonMemory - used) { return nullptr; }
    auto p = static_cast<Allocation*>(malloc(sizeof(Allocation) + size));
    if (!p) { return nullptr; }
    p->size = size; used += size; if (used > peak) { peak = used; } return p + 1;
}
void BoundedAllocator::deallocate(void* pointer) {
    if (!pointer) { return; }
    auto p = static_cast<Allocation*>(pointer) - 1; used -= p->size; free(p);
}
void* BoundedAllocator::reallocate(void* pointer, size_t size) {
    if (!pointer) { return allocate(size); }
    auto p = static_cast<Allocation*>(pointer) - 1; size_t old = p->size;
    if (size > MaxJsonMemory - (used - old)) { return nullptr; }
    auto replacement = static_cast<Allocation*>(realloc(p, sizeof(Allocation) + size));
    if (!replacement) { return nullptr; }
    replacement->size = size; used = used - old + size; if (used > peak) { peak = used; } return replacement + 1;
}
char const* typeName(Type t) {
    switch (t) {
        case Type::Bool: return "bool";
        case Type::Uint16: return "uint16";
        case Type::Int16: return "int16";
        case Type::Uint32: return "uint32";
        case Type::Int32: return "int32";
        case Type::Float32: return "float32";
    }
    return "unsupported";
}
char const* spaceName(Modbus::ReadFunction f) {
    switch (f) {
        case Modbus::ReadFunction::DiscreteInputs: return "discrete";
        case Modbus::ReadFunction::InputRegisters: return "input";
        case Modbus::ReadFunction::HoldingRegisters: return "holding";
    }
    return "unsupported";
}
bool validId(char const* id) {
    if (!id || !*id || strlen(id) > MaxId || id[0] < 'a' || id[0] > 'z') { return false; }
    for (auto p = id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) { return false; }
    }
    return true;
}
bool parse(char const* json, size_t size, Profile& output, char const*& error) {
    error = "Profile exceeds size limit or contains ambiguous JSON (duplicate keys, nesting, escapes, non-ASCII or trailing data)";
    if (!json || size == 0 || size > MaxJson || !Gate(json, size).run()) { return false; }
    BoundedAllocator allocator;
    JsonDocument doc(&allocator);
    error = "Invalid JSON or JSON allocation limit exceeded";
    auto parseError = deserializeJson(doc, json, size, DeserializationOption::NestingLimit(4));
    if (parseError) {
        if (parseError == DeserializationError::NoMemory) { error = "Insufficient memory"; }
        return false;
    }
    auto candidate = std::unique_ptr<Profile>(new (std::nothrow) Profile());
    error = "Insufficient memory"; if (!candidate) { return false; }
    auto& p = *candidate;
    auto root = doc.as<JsonObjectConst>();
    error = "Unknown schema/fields or invalid profile id";
    if (!fields(root, { "schema", "id", "capabilities", "metadata", "transport", "telemetry", "provenance" })
            || !equals(root["schema"], Schema) || !text(root["id"], p.id, sizeof(p.id)) || !validId(p.id)) { return false; }
    error = "Invalid or unsupported profile capabilities";
    if (!parseCapabilities(root["capabilities"], p)) { return false; }
    auto meta = root["metadata"].as<JsonObjectConst>();
    auto provenance = root["provenance"].as<JsonObjectConst>();
    error = "Required metadata/provenance missing, overlong or unknown fields";
    if (!fields(meta, { "vendor", "model", "source", "source_revision", "description" })
            || !text(meta["vendor"], p.vendor, sizeof(p.vendor)) || !text(meta["model"], p.model, sizeof(p.model))
            || !text(meta["source"], p.source, sizeof(p.source)) || !text(meta["source_revision"], p.revision, sizeof(p.revision))
            || !text(meta["description"], p.description, sizeof(p.description), meta["description"].isUnbound())
            || !fields(provenance, { "author", "license" })
            || !text(provenance["author"], p.author, sizeof(p.author)) || !text(provenance["license"], p.license, sizeof(p.license))) { return false; }
    auto transport = root["transport"].as<JsonObjectConst>();
    error = "Invalid transport (modbus_tcp, port 1..65535, unit 1..247, address_base 0/1 required)";
    if (!fields(transport, { "protocol", "default_port", "unit_id", "address_base" })
            || !equals(transport["protocol"], "modbus_tcp") || !transport["default_port"].is<uint16_t>()
            || transport["default_port"].as<uint16_t>() == 0 || !transport["unit_id"].is<uint8_t>()
            || transport["unit_id"].as<uint8_t>() == 0 || transport["unit_id"].as<uint8_t>() > 247
            || !transport["address_base"].is<uint8_t>() || transport["address_base"].as<uint8_t>() > 1) { return false; }
    p.port = transport["default_port"]; p.unit = transport["unit_id"]; p.addressBase = transport["address_base"];
    auto entries = root["telemetry"].as<JsonArrayConst>();
    error = "Telemetry must contain 1..24 entries";
    if (entries.isNull() || entries.size() == 0 || entries.size() > MaxEntries) { return false; }
    for (auto value : entries) {
        auto obj = value.as<JsonObjectConst>(); auto& e = p.entries[p.entryCount];
        error = "Invalid telemetry key/unit or unknown telemetry fields";
        if (!fields(obj, { "key", "space", "address", "count", "type", "byte_order", "word_order", "scale", "offset", "unit", "min", "max", "invalid_raw" })
                || !text(obj["key"], e.key, sizeof(e.key)) || !validId(e.key)
                || !text(obj["unit"], e.unit, sizeof(e.unit), obj["unit"].isUnbound())) { return false; }
        error = "Duplicate telemetry key";
        for (size_t i = 0; i < p.entryCount; ++i) { if (!strcmp(e.key, p.entries[i].key)) { return false; } }
        bool found = false;
        for (auto space : { Modbus::ReadFunction::DiscreteInputs, Modbus::ReadFunction::InputRegisters, Modbus::ReadFunction::HoldingRegisters }) {
            if (equals(obj["space"], spaceName(space))) { e.space = space; found = true; break; }
        }
        error = "Unsupported register space"; if (!found) { return false; }
        found = false;
        for (auto type : { Type::Bool, Type::Uint16, Type::Int16, Type::Uint32, Type::Int32, Type::Float32 }) {
            if (equals(obj["type"], typeName(type))) { e.type = type; found = true; break; }
        }
        error = "Unsupported type"; if (!found) { return false; }
        error = "Invalid address/count/type combination";
        if (!obj["address"].is<uint32_t>() || !obj["count"].is<uint8_t>()) { return false; }
        uint32_t address = obj["address"]; e.count = obj["count"];
        uint8_t expected = e.type == Type::Uint32 || e.type == Type::Int32 || e.type == Type::Float32 ? 2 : 1;
        if (address < p.addressBase || address - p.addressBase > 65535 || e.count != expected
                || address - p.addressBase + e.count > 65536
                || ((e.space == Modbus::ReadFunction::DiscreteInputs) != (e.type == Type::Bool))) { return false; }
        e.address = address - p.addressBase;
        error = "Invalid ordering";
        if (!obj["byte_order"].isUnbound()) {
            if (!equals(obj["byte_order"], "big") && !equals(obj["byte_order"], "little")) { return false; }
            e.littleBytes = equals(obj["byte_order"], "little");
        }
        if (!obj["word_order"].isUnbound()) {
            if (!equals(obj["word_order"], "high_first") && !equals(obj["word_order"], "low_first")) { return false; }
            e.lowWordFirst = equals(obj["word_order"], "low_first");
        }
        if ((e.count == 1 && e.lowWordFirst) || (e.type == Type::Bool && e.littleBytes)) { return false; }
        error = "Invalid scale/offset/range/sentinel";
        if (!obj["scale"].isUnbound() && (!numeric(obj["scale"], e.scale) || e.scale == 0)) { return false; }
        if (!obj["offset"].isUnbound() && !numeric(obj["offset"], e.offset)) { return false; }
        e.hasMinimum = !obj["min"].isUnbound(); e.hasMaximum = !obj["max"].isUnbound(); e.hasInvalidRaw = !obj["invalid_raw"].isUnbound();
        if ((e.hasMinimum && !numeric(obj["min"], e.minimum)) || (e.hasMaximum && !numeric(obj["max"], e.maximum))
                || (e.hasMinimum && e.hasMaximum && e.minimum > e.maximum)) { return false; }
        if (e.hasInvalidRaw) {
            if (!obj["invalid_raw"].is<uint32_t>()) { return false; }
            e.invalidRaw = obj["invalid_raw"];
            if (e.count == 1 && e.invalidRaw > 65535) { return false; }
        }
        if (e.type == Type::Bool && (e.scale != 1 || e.offset != 0 || e.hasMinimum || e.hasMaximum || e.hasInvalidRaw)) { return false; }
        ++p.entryCount;
    }
    // Ensure normalization can be exported within the same hard bounds.
    doc.clear(); serialize(p, doc.to<JsonObject>());
    error = "Normalized profile exceeds JSON memory/export limit";
    if (doc.overflowed()) { error = "Insufficient memory"; return false; }
    if (measureJson(doc) > MaxJson) { return false; }
    output = p; error = nullptr; return true;
}
void serialize(Profile const& p, JsonObject root) {
    root["schema"] = Schema; root["id"] = static_cast<char const*>(p.id);
    if (p.capabilities) {
        auto capabilities = root["capabilities"].to<JsonArray>();
        if (hasCapability(p, Capability::Storage)) { capabilities.add("storage"); }
    }
    auto m = root["metadata"].to<JsonObject>();
    m["vendor"] = static_cast<char const*>(p.vendor); m["model"] = static_cast<char const*>(p.model); m["source"] = static_cast<char const*>(p.source); m["source_revision"] = static_cast<char const*>(p.revision);
    if (*p.description) { m["description"] = static_cast<char const*>(p.description); }
    auto provenance = root["provenance"].to<JsonObject>(); provenance["author"] = static_cast<char const*>(p.author); provenance["license"] = static_cast<char const*>(p.license);
    auto t = root["transport"].to<JsonObject>(); t["protocol"] = "modbus_tcp"; t["default_port"] = p.port;
    t["unit_id"] = p.unit; t["address_base"] = p.addressBase;
    auto entries = root["telemetry"].to<JsonArray>();
    for (size_t i = 0; i < p.entryCount; ++i) {
        auto const& e = p.entries[i]; auto o = entries.add<JsonObject>();
        o["key"] = static_cast<char const*>(e.key); o["space"] = spaceName(e.space); o["address"] = static_cast<uint32_t>(e.address) + p.addressBase;
        o["count"] = e.count; o["type"] = typeName(e.type);
        o["byte_order"] = e.littleBytes ? "little" : "big"; o["word_order"] = e.lowWordFirst ? "low_first" : "high_first";
        o["scale"] = e.scale; o["offset"] = e.offset;
        if (*e.unit) { o["unit"] = static_cast<char const*>(e.unit); }
        if (e.hasMinimum) { o["min"] = e.minimum; }
        if (e.hasMaximum) { o["max"] = e.maximum; }
        if (e.hasInvalidRaw) { o["invalid_raw"] = e.invalidRaw; }
    }
}
Value decode(Entry const& e, uint8_t const* payload, size_t size) {
    Value result; result.type = e.type;
    size_t expected = e.type == Type::Bool ? 1 : e.count * 2;
    if (!payload || size != expected || e.count != (e.type == Type::Uint32 || e.type == Type::Int32 || e.type == Type::Float32 ? 2 : 1)) { return result; }
    if (e.type == Type::Bool) { result.valid = true; result.boolean = payload[0] & 1; return result; }
    uint32_t bits = 0;
    for (size_t i = 0; i < e.count; ++i) {
        size_t word = e.lowWordFirst ? e.count - 1 - i : i;
        auto b = payload + word * 2;
        uint16_t v = e.littleBytes ? (static_cast<uint16_t>(b[1]) << 8) | b[0] : (static_cast<uint16_t>(b[0]) << 8) | b[1];
        bits = (bits << 16) | v;
    }
    if (e.hasInvalidRaw && bits == e.invalidRaw) { return result; }
    double value;
    switch (e.type) {
        case Type::Uint16: case Type::Uint32: value = bits; break;
        case Type::Int16: value = bits & 0x8000 ? static_cast<double>(bits) - 65536 : bits; break;
        case Type::Int32: value = bits & 0x80000000 ? static_cast<double>(bits) - 4294967296.0 : bits; break;
        case Type::Float32: { float f; memcpy(&f, &bits, sizeof(f)); value = f; break; }
        default: return result;
    }
    value = value * e.scale + e.offset;
    if (!std::isfinite(value) || (e.hasMinimum && value < e.minimum) || (e.hasMaximum && value > e.maximum)) { return result; }
    result.valid = true; result.number = value; return result;
}
char const* exampleJson() {
    return R"({"schema":"opendtu-hems-modbus-profile/v1","id":"example.synthetic","metadata":{"vendor":"Example only","model":"Synthetic decoder fixture - not a device","source":"OpenDTU-HEMS","source_revision":"v1"},"provenance":{"author":"OpenDTU-HEMS contributors","license":"GPL-2.0-or-later"},"transport":{"protocol":"modbus_tcp","default_port":502,"unit_id":1,"address_base":0},"telemetry":[{"key":"soc_pct","space":"holding","address":0,"count":1,"type":"uint16","scale":0.1,"unit":"percent","min":0,"max":100}]})";
}
} // namespace Hems::Profiles
