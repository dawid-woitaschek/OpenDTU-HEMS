# Generic Modbus storage telemetry (PR #5)

PR #5 adds a manufacturer-neutral, read-only storage provider on top of the Modbus profile engine introduced in PR #4.

## Architecture boundary

```text
Modbus profile
    ↓
Generic Modbus Storage Provider
    ↓
Storage telemetry snapshot
    ↓
PR #6 normalized HEMS state / constraints
```

The provider must not contain manufacturer-specific register addresses or mode logic. AC- and DC-coupled storage systems are peers: a profile may expose AC power, DC power, both, or neither. Missing optional values stay unknown/null.

## Initial semantic telemetry keys

The runtime may consume these semantics when present in a selected profile:

- `soc_pct`
- `ac_power_w`
- `dc_power_w`
- `battery_voltage_v`
- `battery_current_a`
- `temperature_c`
- `charge_energy_total_kwh`
- `discharge_energy_total_kwh`
- `operating_state`
- `fault_code`
- `alarm_code`

This list is a runtime contract, not a requirement that every storage profile expose every key.

Storage power uses the HEMS sign convention:

```text
positive = charging
negative = discharging
```

Profiles are responsible for device-specific scaling/sign conversion needed to reach that convention.

## Safety / non-goals

PR #5 is telemetry-only. It does not add Modbus write requests, storage control commands, external-control permission, Shadow/Active policy execution or Safety Supervisor write decisions. Those remain later milestones.

## First reference device

Marstek Venus E 3.0 is the first reference profile/hardware because register material and prior target observations are available. It is not a special-case provider. Any Marstek-specific register address belongs in a profile or provenance documentation, never in the generic provider.

Real Venus E validation may remain explicitly deferred at merge time, in which case PR #5 stays 🟡 Validation rather than being falsely marked fully completed.
