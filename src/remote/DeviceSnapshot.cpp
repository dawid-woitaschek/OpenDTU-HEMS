// SPDX-License-Identifier: GPL-2.0-or-later
#include <remote/Snapshots.h>
#include <hems/Controller.h>
#include <hems/storage/SettingsStore.h>
namespace Remote {
// Isolated frames: device snapshot copies are gone before entering TLS.
__attribute__((noinline)) void deviceSnapshot(JsonObject storage, JsonObject wallbox) {
    storageJson(HemsController.getStorageSnapshot(), storage);
    wallboxJson(HemsController.getWarpSnapshot(), wallbox);
    auto config = Hems::Storage::StorageSettings.get();
    storage["profile"] = static_cast<char const*>(config.Profile);
    char profile[49] {};
    HemsController.getControlProfileId(Hems::ControlTarget::Evse, profile, sizeof(profile));
    wallbox["profile"] = static_cast<char const*>(profile);
    // Read existing normalized limits/status without including policies, history or schedules.
    auto command = HemsController.getActuationCommandSnapshot(Hems::ControlTarget::Evse);
    wallbox["last_hems_command"]["state"] = Hems::Actuation::mailboxStateName(command.state);
    if (command.id) {
        wallbox["last_hems_command"]["action"] = Hems::Actuation::actionName(command.command.action);
        if (command.command.hasValue) { wallbox["last_hems_command"]["value"] = command.command.value; }
    }
    auto limits = HemsController.getStoragePowerLimits();
    auto constraints = HemsController.getConstraints();
    storage["charge_allowed"] = constraints.storage.chargeAllowed;
    storage["discharge_allowed"] = constraints.storage.dischargeAllowed;
    storage["power_limits"]["known"] = limits.known;
    storage["power_limits"]["charge_w"] = nullptr; storage["power_limits"]["discharge_w"] = nullptr;
    if (limits.known) { storage["power_limits"]["charge_w"] = limits.chargeW; storage["power_limits"]["discharge_w"] = limits.dischargeW; }
}
}
