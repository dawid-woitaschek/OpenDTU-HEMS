// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/storage/Settings.h>
#include <mutex>

namespace Hems::Storage {

class SettingsStore {
public:
    void init();
    StorageConfig get() const;
    bool set(StorageConfig const& config);

private:
    mutable std::mutex _mutex;
    StorageConfig _config {};
};

extern SettingsStore StorageSettings;

} // namespace Hems::Storage
