// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace Hems::Model {

enum class Quality : uint8_t {
    Unknown = 0,
    Fresh,
    Stale,
};

template <typename T>
struct Signal {
    T value {};
    Quality quality = Quality::Unknown;

    bool known() const { return quality != Quality::Unknown; }
    bool usable() const { return quality == Quality::Fresh; }

    static Signal fresh(T value) { return { value, Quality::Fresh }; }
    static Signal stale(T value) { return { value, Quality::Stale }; }
};

struct DeviceState {
    bool configured = false;
    bool enabled = false;
    bool reachable = false;
    bool hasUpdate = false;
    uint32_t ageMs = 0;
};

struct GridState {
    Signal<double> powerW;
};

struct PvState {
    // AC output of solar-classified Hoymiles inverters. DC solar chargers are
    // deliberately not mixed into this signal because they represent a
    // different energy path and would distort grid-side calculations.
    Signal<double> powerW;
};

struct StoragePowerLimits {
    bool known = false;
    uint32_t chargeW = 0, dischargeW = 0;
};

struct StorageState {
    DeviceState device;
    Signal<double> socPct;
    Signal<double> acPowerW; // Positive = charging, negative = discharging.
    Signal<double> dcPowerW; // Positive = charging, negative = discharging.
    Signal<double> batteryVoltageV;
    Signal<double> batteryCurrentA;
    Signal<double> temperatureC;
    Signal<bool> charging;
    Signal<bool> discharging;
    Signal<bool> faultActive;
    Signal<bool> alarmActive;
};

struct EvseState {
    DeviceState device;
    Signal<bool> vehicleConnected;
    Signal<bool> charging;
    Signal<double> powerW;
    Signal<double> allowedCurrentA;
};

struct SystemState {
    GridState grid;
    PvState pv;
    StorageState storage;
    EvseState evse;
};

} // namespace Hems::Model
