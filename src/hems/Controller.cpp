// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <Configuration.h>

Hems::Controller HemsController;

namespace Hems {

void Controller::init(Scheduler& scheduler)
{
    scheduler.addTask(_loopTask);
    _loopTask.setCallback([this] { _warp.loop(); });
    _loopTask.setInterval(100);
    _loopTask.setIterations(TASK_FOREVER);
    _loopTask.enable();
    updateSettings();
}

void Controller::updateSettings()
{
    auto const& config = Configuration.get().Hems;
    // Unknown schemas must not enable HEMS or external polling.
    _mode.store(config.Version == CONFIG_VERSION_HEMS && config.Enabled
            ? Mode::Monitor : Mode::Disabled);
    _warp.configure(config.Warp, getMode() == Mode::Monitor);
}

Mode Controller::getMode() const
{
    return _mode.load();
}

Warp::Snapshot Controller::getWarpSnapshot() const
{
    return _warp.snapshot();
}

} // namespace Hems
