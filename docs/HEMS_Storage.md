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

## Profile capability and consumer selection

A Modbus profile is not assigned to a HEMS consumer merely because one of its telemetry keys happens to look familiar. Device classes can legitimately share semantic names such as `charging`, `temperature_c` or `fault_code`; inferring identity from those keys would allow a wallbox profile to appear as a storage profile.

PR #5 therefore uses an explicit profile capability:

```json
"capabilities": ["storage"]
```

The field is optional at the profile-engine level so existing PR #4 profiles continue to parse and can still be viewed, exported or decoded. A storage device may only select a profile when both conditions are true:

1. the profile explicitly declares the `storage` capability; and
2. it contains at least one semantic understood by the generic storage provider.

A capability without usable storage telemetry is rejected, and recognized storage-like telemetry without the capability is not enough. The same pattern is intended for later HEMS consumers such as EVSE/wallbox profiles, avoiding cross-category dropdowns without introducing manufacturer-specific logic.

The profile API reports capabilities together with current consumers. A profile referenced by storage cannot be deleted; the API returns HTTP 409 and identifies the `storage` consumer. Disabling storage removes its profile assignment, after which the profile may be deleted. The backend enforces these rules independently of the Web UI.

## Initial semantic telemetry keys

The runtime consumes these semantics when present in an explicitly storage-capable selected profile.

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

The native fixtures deliberately cover AC-only and DC-only storage telemetry, FC2 boolean telemetry, numeric 0/1 flags, valid zero/false, partial validity, ignored vendor-specific keys, explicit storage capability enforcement and communication failure/staleness propagation.

## Runtime profile access

The Web API and storage runtime share one LittleFS-backed profile store and one operation mutex. The provider resolves the selected profile while configuring and keeps an immutable shared snapshot for polling, so an import/replace/delete operation cannot mutate the profile underneath an in-flight network poll.

Successful profile replacement or deletion triggers a provider reconfiguration only after the profile-store lock has been released. A referenced profile cannot be deleted. A compatible replacement is reloaded immediately; an incompatible replacement is rejected before persistence.

Only the selected profile is retained by the provider; there is no persistent in-memory registry of all imported profiles. PR #5 therefore remains compatible with the no-PSRAM targets.

## Runtime device configuration

Storage connection settings are part of the HEMS API contract at `/api/hems/config`:

```json
{
  "storage": {
    "enabled": true,
    "host": "storage.local",
    "profile": "vendor.model",
    "port": 0,
    "unit": 0
  }
}
```

`port = 0` and `unit = 0` mean that the selected profile transport defaults are used. Non-zero device settings override those defaults. Hostnames use the same bounded hostname validation as the shared Modbus client. Modbus unit IDs above 247 are rejected.

When storage telemetry is enabled, host and an existing compatible storage profile are required. Disabling storage deliberately clears the profile assignment so a disabled configuration cannot retain a hidden reference that later blocks profile deletion or fails unexpectedly when re-enabled.

The storage settings are persisted independently from the inherited OpenDTU `config.json` in the versioned LittleFS file `/hems-storage.json`. The current file schema version is `1`. Malformed or unsupported versions fail safe to an unconfigured/disabled storage provider. The file is bounded to 1024 bytes, written through a temporary file, parsed and validated again before rename, and is visible through the inherited configuration file-management API. A full factory reset removes it together with the other LittleFS configuration files.

Keeping this small HEMS-specific sidecar separate avoids invasive changes to the large inherited OpenDTU configuration schema during the telemetry-only milestone. The public HEMS API still presents storage configuration together with the other HEMS device settings.

## Runtime status and UI

`/api/hems/status` exposes storage reachability, selected profile, resolved Modbus port/unit, freshness, last error, successful/failed poll counters and the semantic telemetry fields. Operational telemetry fields are returned as `null` unless the current storage snapshot is valid; stale or failed reads therefore cannot masquerade as fresh zero/false values.

The HEMS device page configures host, profile and optional transport overrides. Its storage dropdown contains only explicitly storage-capable profiles with supported storage telemetry. Missing persisted profile references are shown as an explicit configuration error instead of an empty selection. If no compatible profile exists, the UI says so and prevents enabling storage without choosing one.

The wallbox section is user-facing as `Wallbox`; WARP remains the current read-only provider implementation. Provider names are not used as HEMS device-category names. The synthetic decoder fixture `example.synthetic` remains available to the profile engine but is not assignable to storage. The HEMS overview renders read-only device telemetry and diagnostics. Multi-section HEMS pages use the inherited OpenDTU section-spacing convention (`add-space`/`mt-5`).

## Safety / non-goals

PR #5 is telemetry-only. It does not add Modbus write requests, storage control commands, external-control permission, Shadow/Active policy execution or Safety Supervisor write decisions. Those remain later milestones.

## First reference device

Marstek Venus E 3.0 is the first reference profile/hardware because register material and prior target observations are available. It is not a special-case provider. Any Marstek-specific register address belongs in a profile or provenance documentation, never in the generic provider.

The candidate `docs/examples/marstek-venus-e3-observed.json` explicitly declares `capabilities: ["storage"]`, pins the reviewed community source revision and deliberately overrides only the target observations already seen on the family device: SoC scaling `0.01` at register `34002` and AC-power sign scaling `-1` at register `30006`. The community E3 map itself explicitly states that it is only partially hardware-validated, so this candidate is not presented as a universal or fully validated Marstek profile.

Real Venus E validation may remain explicitly deferred at merge time. That limitation must remain visible rather than being converted into a fictional hardware-validation claim.
