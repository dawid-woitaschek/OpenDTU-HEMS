// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <hems/evse/Settings.h>
#include <mutex>

namespace Hems::Evse {

class SettingsStore {
public:
    void init();
    BindingConfig get() const;
    bool set(BindingConfig const& config);

private:
    mutable std::mutex _mutex;
    BindingConfig _config {};
};

extern SettingsStore BindingSettings;

} // namespace Hems::Evse
