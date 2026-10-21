// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/Store.h>
#include <cstring>
#include <memory>

namespace Hems::Profiles {
bool Store::builtin(Profile& output, char const*& error) {
#ifdef HEMS_TEST_BUILTIN_PROFILE
    auto json = exampleJson(); return parse(json, strlen(json), output, error);
#else
    (void)output;
    error = "No built-in profiles";
    return false;
#endif
}
bool Store::load(size_t slot, Profile& output, char const*& error) {
    error = "Missing/corrupt imported profile";
    if (slot >= MaxImported) { return false; }
    auto size = _files.size(slot);
    if (size == SIZE_MAX) { error = "Profile read failed"; return false; }
    if (!size || size > MaxJson) { return false; }
    auto buffer = std::unique_ptr<char[]>(new (std::nothrow) char[size]);
    error = "Insufficient memory"; if (!buffer) { return false; }
    error = "Profile read failed";
    return _files.read(slot, buffer.get(), size) && parse(buffer.get(), size, output, error);
}
bool Store::get(char const* id, Profile& output, bool& builtIn, char const*& error) {
    error = "Invalid profile id"; if (!validId(id)) { return false; }
#ifdef HEMS_TEST_BUILTIN_PROFILE
    builtIn = !strcmp(id, "example.synthetic");
    if (builtIn) { return builtin(output, error); }
#else
    builtIn = false;
#endif
    auto p = std::unique_ptr<Profile>(new (std::nothrow) Profile());
    error = "Insufficient memory"; if (!p) { return false; }
    size_t found = 0;
    for (size_t slot = 0; slot < MaxImported; ++slot) {
        if (load(slot, *p, error) && !strcmp(id, p->id)) { output = *p; ++found; }
    }
    error = found > 1 ? "Ambiguous imported profile id" : "Profile not found";
    if (found != 1) { return false; }
    error = nullptr; return true;
}
bool Store::import(char const* json, size_t size, char const* replaceId, char const*& error) {
    auto candidate = std::unique_ptr<Profile>(new (std::nothrow) Profile());
    auto stored = std::unique_ptr<Profile>(new (std::nothrow) Profile());
    error = "Insufficient memory"; if (!candidate || !stored) { return false; }
    if (!parse(json, size, *candidate, error)) { return false; }
#ifdef HEMS_TEST_BUILTIN_PROFILE
    error = "Built-in profile id is reserved";
    if (!strcmp(candidate->id, "example.synthetic")) { return false; }
#endif
    error = "Replacement id must match the imported JSON id";
    if (replaceId && (!validId(replaceId) || strcmp(candidate->id, replaceId))) { return false; }
    size_t free = MaxImported, match = MaxImported, matches = 0;
    for (size_t slot = 0; slot < MaxImported; ++slot) {
        // Corrupt slots cannot prevent startup and may be reused by a validated import.
        if (!load(slot, *stored, error)) {
            if (error && (!strcmp(error, "Insufficient memory") || !strcmp(error, "Profile read failed"))) { return false; }
            if (free == MaxImported) { free = slot; }
        } else if (!strcmp(stored->id, candidate->id)) { match = slot; ++matches; }
    }
    error = "Duplicate profile id; use explicit replacement";
    if (matches > 1 || (matches && !replaceId)) { return false; }
    error = "Replacement profile not found"; if (replaceId && !matches) { return false; }
    size_t slot = matches ? match : free;
    error = "Imported profile count limit reached"; if (slot == MaxImported) { return false; }
    stored.reset(); // Slot discovery is complete; do not overlap this copy with export/commit scratch.
    BoundedAllocator allocator; JsonDocument doc(&allocator); serialize(*candidate, doc.to<JsonObject>());
    size_t normalizedSize = measureJson(doc);
    error = "Export size/memory limit exceeded";
    if (doc.overflowed() || normalizedSize > MaxJson) { return false; }
    auto normalized = std::unique_ptr<char[]>(new (std::nothrow) char[normalizedSize + 1]);
    error = "Insufficient memory"; if (!normalized) { return false; }
    serializeJson(doc, normalized.get(), normalizedSize + 1);
    error = "Flash commit failed; existing profile retained";
    if (!_files.commit(slot, normalized.get(), normalizedSize)) { return false; }
    error = nullptr; return true;
}
bool Store::erase(char const* id, char const*& error) {
    error = "Invalid profile id"; if (!validId(id)) { return false; }
#ifdef HEMS_TEST_BUILTIN_PROFILE
    error = "Built-in profiles cannot be deleted"; if (!strcmp(id, "example.synthetic")) { return false; }
#endif
    auto p = std::unique_ptr<Profile>(new (std::nothrow) Profile());
    error = "Insufficient memory"; if (!p) { return false; }
    size_t match = MaxImported, count = 0;
    for (size_t slot = 0; slot < MaxImported; ++slot) {
        if (load(slot, *p, error) && !strcmp(p->id, id)) { match = slot; ++count; }
    }
    error = "Profile missing or ambiguous"; if (count != 1) { return false; }
    error = "Delete failed"; if (!_files.remove(match)) { return false; }
    error = nullptr; return true;
}
} // namespace Hems::Profiles
