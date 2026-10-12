// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/Controller.h>
#include <hems/storage/SettingsStore.h>
#include <Configuration.h>

Hems::Controller HemsController;

namespace Hems {

void Controller::init(Scheduler& scheduler)
{
    Storage::StorageSettings.init();
    scheduler.addTask(_loopTask);
    _loopTask.setCallback([this] {
        _warp.loop();
        _storage.loop();
    });
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
    bool monitor = getMode() == Mode::Monitor;
    _warp.configure(config.Warp, monitor);
    _storage.configure(Storage::StorageSettings.get(), monitor);
}

Mode Controller::getMode() const
{
    return _mode.load();
}

Warp::Snapshot Controller::getWarpSnapshot() const
{
    return _warp.snapshot();
}

Storage::Snapshot Controller::getStorageSnapshot() const
{
    return _storage.snapshot();
}

} // namespace Hems
