// SPDX-License-Identifier: GPL-2.0-or-later
#include <hems/evse/SettingsStore.h>
#include <hems/storage/SettingsStore.h>
#include <Configuration.h>
#include <LittleFS.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace Hems;
int main()
{
    Evse::SettingsStore evse; Storage::SettingsStore storage;
    Evse::BindingConfig binding; strcpy(binding.Profile, "tinkerforge.warp3-smart"); binding.Control = ControlMode::HemsControl;
    Configuration.data.Hems.Warp.Enabled = true;
    assert(!evse.set({})); // Enabled EVSE requires an explicit profile.
    assert(evse.set(binding));
    Evse::SettingsStore reload; reload.init();
    assert(!strcmp(reload.get().Profile, binding.Profile) && reload.get().Control == ControlMode::HemsControl);
    failCommit = true; assert(!evse.clear()); failCommit = false;
    assert(evse.get().Control == ControlMode::HemsControl); // Persistence failure leaves current binding intact.
    assert(evse.clear());
    Configuration.data.Hems.Warp.Enabled = false;
    assert(!evse.get().Profile[0] && evse.get().Control == ControlMode::MonitorOnly);
    assert(evse.set(binding)); // Disabled consumer persists canonical unbound / monitor state.
    Evse::SettingsStore disabled; disabled.init();
    Configuration.data.Hems.Warp.Enabled = true;
    assert(!disabled.get().Profile[0] && disabled.get().Control == ControlMode::MonitorOnly);
    Evse::SettingsStore reenabled; reenabled.init();
    assert(!reenabled.get().Profile[0] && reenabled.get().Control == ControlMode::MonitorOnly);
    assert(!reenabled.set({}));
    assert(reenabled.set(binding)); // Explicit selection/control works again without a reboot.
    binding.Control = ControlMode::MonitorOnly; assert(reenabled.set(binding));
    Evse::SettingsStore monitor; monitor.init(); assert(monitor.get().Control == ControlMode::MonitorOnly);
    // Storage disabled payloads similarly clear hidden binding/control state before persistence.
    JsonDocument doc; auto json = doc.to<JsonObject>();
    json["enabled"] = false; json["profile"] = "marstek.venus-e3.v148"; json["control_mode"] = "hems";
    json["host"] = "storage.local"; json["port"] = 502; json["unit"] = 1;
    StorageConfig candidate;
    assert(deserializeStorageConfig(doc.as<JsonVariantConst>(), candidate));
    assert(!candidate.Enabled && !candidate.Profile[0] && candidate.Control == ControlMode::MonitorOnly);
    assert(storage.set(candidate));
    Storage::SettingsStore storageReload; storageReload.init();
    assert(!storageReload.get().Profile[0] && storageReload.get().Control == ControlMode::MonitorOnly);
    std::cout << "Production EVSE/storage settings persistence, disable/unbind/re-enable and permission reload tests passed\n";
}
