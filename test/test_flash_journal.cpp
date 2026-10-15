// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise production VFS persistence with real native files and faulted syscalls.
#include <hems/recovery/Journal.h>
#include <LittleFS.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace Hems;
using namespace Hems::Recovery;
static bool failSync, failRename, shortWrite;
extern "C" {
int __real_fsync(int);
ssize_t __real_write(int, void const*, size_t);
int __real_rename(char const*, char const*);
int __wrap_fsync(int fd) { if (failSync) { errno = EIO; return -1; } return __real_fsync(fd); }
int __wrap_rename(char const* a, char const* b) { if (failRename) { errno = EIO; return -1; } return __real_rename(a,b); }
ssize_t __wrap_write(int fd, void const* p, size_t n) { return __real_write(fd, p, shortWrite ? n/2 : n); }
}
int main() {
    auto& files = runtimeFiles(); constexpr auto target = ControlTarget::Storage;
    Record record; record.target = target; record.possibleOwnership = true;
    strcpy(record.endpoint.host, "synthetic.invalid"); record.endpoint.port = 502; record.endpoint.unit = 1;
    strcpy(record.profile, "test.flash"); record.compatibilitySize = 1;
    uint8_t bytes[MaxRecord]; size_t size; assert(encode(record, bytes, size));
    auto root = std::filesystem::path(HEMS_RECOVERY_TEST_ROOT);
    for (unsigned failure = 0; failure < 4; ++failure) {
        std::filesystem::remove_all(root); std::filesystem::create_directories(root);
        assert(files.replace(target, bytes, size));
        // Power loss/error before atomic replacement leaves the committed intent
        // plus pending evidence. No failed fsync may be reported as durable.
        shortWrite = failure == 1; failSync = failure == 2; failRename = failure == 3;
        Record tombstone; tombstone.target = target; uint8_t cleared[MaxRecord]; size_t length;
        assert(encode(tombstone, cleared, length));
        bool ok = files.replace(target, cleared, length);
        assert(ok == (failure == 0));
        shortWrite = failSync = failRename = false;
        Journal rebooted(target); rebooted.load(files);
        assert(rebooted.blocked() == (failure != 0));
        if (failure) { assert(rebooted.snapshot().reason == Reason::JournalIncomplete); }
    }
    LittleFS.mounted = false;
    Journal unmounted(target); unmounted.load(files);
    assert(unmounted.snapshot().reason == Reason::JournalIo && unmounted.blocked());
    assert(!files.replace(target, bytes, size));
    std::filesystem::remove_all(root);
    std::cout << "Production journal VFS ordering, fsync, rename and interrupted-clear tests passed\n";
}
