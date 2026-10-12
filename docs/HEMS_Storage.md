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

The runtime consumes these semantics when present in a selected profile.

Numeric semantics:

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

Discrete/boolean semantics:

- `charging`
- `discharging`
- `fault_active`
- `alarm_active`

This list is a runtime contract, not a requirement that every storage profile expose every key. Unknown/vendor-specific telemetry keys remain legal profile data but are ignored by the generic storage poller. This lets one profile carry extra diagnostics without forcing the provider to learn manufacturer-specific meanings.

Each recognized semantic field carries its own validity. A successful poll therefore does not imply that every optional field exists or is valid. A profile must yield at least one recognized, valid storage value to count as a valid storage poll. Valid numeric zero and valid boolean `false` remain valid values rather than becoming unknown.

Storage power uses the HEMS sign convention:

```text
positive = charging
negative = discharging
```

Profiles are responsible for device-specific scaling/sign conversion needed to reach that convention.

## Read path

The generic poller only uses the already bounded read-only Modbus surface:

- FC2 discrete inputs
- FC3 holding registers
- FC4 input registers

There is no write function in the reader seam. Each recognized entry is decoded with the PR #4 profile decoder, so type, scale, offset, byte order, word order, range and invalid sentinel remain profile concerns rather than storage-provider code.

The first native fixtures deliberately cover AC-only and DC-only storage telemetry, FC2 boolean telemetry, valid zero/false, partial validity, ignored vendor-specific keys and communication failure/staleness propagation.

## Runtime profile access

The Web API and storage runtime share one LittleFS-backed profile store and one operation mutex. The provider resolves the selected profile while configuring and keeps an immutable shared snapshot for polling, so an import/replace/delete operation cannot mutate the profile underneath an in-flight network poll.

Only the selected profile is retained by the provider; there is no persistent in-memory registry of all imported profiles. PR #5 therefore remains compatible with the no-PSRAM targets.

## Safety / non-goals

PR #5 is telemetry-only. It does not add Modbus write requests, storage control commands, external-control permission, Shadow/Active policy execution or Safety Supervisor write decisions. Those remain later milestones.

## First reference device

Marstek Venus E 3.0 is the first reference profile/hardware because register material and prior target observations are available. It is not a special-case provider. Any Marstek-specific register address belongs in a profile or provenance documentation, never in the generic provider.

The candidate `docs/examples/marstek-venus-e3-observed.json` pins the reviewed community source revision and deliberately overrides only the target observations already seen on the family device: SoC scaling `0.01` at register `34002` and AC-power sign scaling `-1` at register `30006`. The community E3 map itself explicitly states that it is only partially hardware-validated, so this candidate is not presented as a universal or fully validated Marstek profile.

Real Venus E validation may remain explicitly deferred at merge time, in which case PR #5 stays 🟡 Validation rather than being falsely marked fully completed.
