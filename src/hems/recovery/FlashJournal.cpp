// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/recovery/Journal.h>
#include <LittleFS.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

#ifndef HEMS_RECOVERY_TEST_ROOT
#define HEMS_RECOVERY_TEST_ROOT "/littlefs"
#endif

namespace Hems::Recovery {
namespace {
class FlashFiles final : public Files {
public:
    LoadResult read(ControlTarget target, bool pending, uint8_t* data, size_t capacity, size_t& size) override {
        std::lock_guard<std::mutex> lock(_mutex);
        return readLocked(target, pending, data, capacity, size);
    }
    bool replace(ControlTarget target, uint8_t const* data, size_t size) override {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!LittleFS.totalBytes() || size > MaxRecord) { return false; }
        auto verified = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[MaxRecord]);
        if (!verified) { return false; }
        // Use VFS return values: Arduino File.flush()/close() return void and
        // cannot prove that lfs_file_sync succeeded. Never erase pending evidence
        // on failure. LittleFS atomically commits rename (no unlink-before-rename).
        int fd = ::open(path(target, true), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) { return false; }
        bool ok = ::write(fd, data, size) == static_cast<ssize_t>(size);
        if (::fsync(fd) != 0) { ok = false; }
        if (::close(fd) != 0) { ok = false; }
        size_t length = 0;
        ok = ok && readLocked(target, true, verified.get(), MaxRecord, length) == LoadResult::Present
            && length == size && !memcmp(verified.get(), data, size);
        if (!ok || ::rename(path(target, true), path(target, false)) != 0) { return false; }
        // LittleFS rename commits its metadata synchronously. Verify namespace
        // and content before allowing an ownership-producing write.
        struct stat info;
        errno = 0;
        bool pendingGone = ::stat(path(target, true), &info) != 0 && errno == ENOENT;
        return pendingGone && readLocked(target, false, verified.get(), MaxRecord, length) == LoadResult::Present
            && length == size && !memcmp(verified.get(), data, size);
    }
private:
    LoadResult readLocked(ControlTarget target, bool pending, uint8_t* data, size_t capacity, size_t& size) {
        size = 0;
        if (!LittleFS.totalBytes()) { return LoadResult::Error; }
        struct stat info;
        if (::stat(path(target, pending), &info) != 0) {
            return errno == ENOENT ? LoadResult::Missing : LoadResult::Error;
        }
        if (!S_ISREG(info.st_mode) || info.st_size < 0 || static_cast<size_t>(info.st_size) > capacity) { return LoadResult::Error; }
        size = info.st_size;
        int fd = ::open(path(target, pending), O_RDONLY);
        if (fd < 0) { return LoadResult::Error; }
        bool ok = ::read(fd, data, size) == static_cast<ssize_t>(size);
        if (::close(fd) != 0) { ok = false; }
        return ok ? LoadResult::Present : LoadResult::Error;
    }
    static char const* path(ControlTarget target, bool pending) {
        if (target == ControlTarget::Storage) {
            return pending ? HEMS_RECOVERY_TEST_ROOT "/hems-recovery-storage.pending" : HEMS_RECOVERY_TEST_ROOT "/hems-recovery-storage.bin";
        }
        return pending ? HEMS_RECOVERY_TEST_ROOT "/hems-recovery-evse.pending" : HEMS_RECOVERY_TEST_ROOT "/hems-recovery-evse.bin";
    }
    std::mutex _mutex;
};
}
Files& runtimeFiles() { static FlashFiles files; return files; }
} // namespace Hems::Recovery
