// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <Configuration.h>

Hems::Controller HemsController;

namespace Hems {

void Controller::init()
{
    updateSettings();
}

void Controller::updateSettings()
{
    auto const& config = Configuration.get().Hems;
    // Unknown schemas must not enable HEMS. No polling task is needed until
    // telemetry exists: this milestone only applies configuration changes.
    _mode.store(config.Version == CONFIG_VERSION_HEMS && config.Enabled
            ? Mode::Monitor : Mode::Disabled);
}

Mode Controller::getMode() const
{
    return _mode.load();
}

} // namespace Hems
