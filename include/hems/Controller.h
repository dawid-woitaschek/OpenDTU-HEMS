// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace Hems {

enum class Mode : uint8_t { Disabled, Monitor };

class Controller {
public:
    void init();
    void updateSettings();
    Mode getMode() const;

private:
    std::atomic<Mode> _mode { Mode::Disabled };
};

} // namespace Hems

extern Hems::Controller HemsController;
