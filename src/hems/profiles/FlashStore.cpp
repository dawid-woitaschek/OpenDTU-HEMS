// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/profiles/FlashStore.h>
#include <LittleFS.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Hems::Profiles {
namespace {

class FlashFiles final : public Files {
public:
    size_t size(size_t slot) override
    {
        if (!LittleFS.exists(path(slot))) { return 0; }
        auto file = LittleFS.open(path(slot), "r");
        return file ? file.size() : SIZE_MAX;
    }

    bool read(size_t slot, char* buffer, size_t length) override
    {
        auto file = LittleFS.open(path(slot), "r");
        return file && file.size() == length && file.readBytes(buffer, length) == length;
    }

    bool commit(size_t slot, char const* buffer, size_t length) override
    {
        constexpr char Temp[] = "/hems-profiles/pending.json";
        constexpr size_t Reserve = 8192;
        if (slot >= MaxImported || length > MaxJson) { return false; }
        LittleFS.remove(Temp);
        if (LittleFS.totalBytes() < LittleFS.usedBytes() + length + Reserve) { return false; }
        if (!LittleFS.exists("/hems-profiles") && !LittleFS.mkdir("/hems-profiles")) { return false; }

        auto file = LittleFS.open(Temp, "w");
        if (!file) { return false; }
        bool ok = file.write(reinterpret_cast<uint8_t const*>(buffer), length) == length;
        file.flush();
        file.close();

        auto verify = LittleFS.open(Temp, "r");
        ok = ok && verify && verify.size() == length;
        uint8_t chunk[128];
        for (size_t offset = 0; ok && offset < length;) {
            size_t n = std::min(sizeof(chunk), length - offset);
            ok = verify.read(chunk, n) == n && !memcmp(chunk, buffer + offset, n);
            offset += n;
        }
        verify.close();
        if (ok) { ok = LittleFS.rename(Temp, path(slot)); }
        if (!ok) { LittleFS.remove(Temp); }
        return ok;
    }

    bool remove(size_t slot) override
    {
        return slot < MaxImported && LittleFS.remove(path(slot));
    }

private:
    static char const* path(size_t slot)
    {
        static char const* const paths[] = {
            "/hems-profiles/0.json", "/hems-profiles/1.json", "/hems-profiles/2.json", "/hems-profiles/3.json"
        };
        return slot < MaxImported ? paths[slot] : "";
    }
};

FlashFiles files;
Store store(files);
std::mutex operation;

} // namespace

Store& runtimeStore() { return store; }
Files& runtimeFiles() { return files; }
std::mutex& runtimeStoreMutex() { return operation; }

} // namespace Hems::Profiles
