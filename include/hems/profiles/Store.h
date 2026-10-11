// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <hems/profiles/Profile.h>

namespace Hems::Profiles {
// Slot numbers, never user-controlled filenames. Backend commits via temp+rename.
class Files {
public:
    virtual ~Files() = default;
    virtual size_t size(size_t slot) = 0; // 0 = absent; oversized/corrupt slots remain isolated
    virtual bool read(size_t slot, char* buffer, size_t size) = 0;
    virtual bool commit(size_t slot, char const* buffer, size_t size) = 0;
    virtual bool remove(size_t slot) = 0;
};
class Store {
public:
    explicit Store(Files& files) : _files(files) {}
    bool load(size_t slot, Profile& output, char const*& error);
    bool get(char const* id, Profile& output, bool& builtIn, char const*& error);
    bool import(char const* json, size_t size, char const* replaceId, char const*& error);
    bool erase(char const* id, char const*& error);
    static bool builtin(Profile& output, char const*& error);
private:
    Files& _files;
};
} // namespace Hems::Profiles
