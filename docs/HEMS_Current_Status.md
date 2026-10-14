# OpenDTU-HEMS current status

> Snapshot date: 2026-10-14
>
> This file is the short, authoritative hand-off for the current HEMS implementation state. `HEMS_Roadmap.md` remains the detailed historical/forward plan, but some of its older PR numbering and milestone text may lag behind the actual merged repository state.

## Current baseline

`hems` baseline at this snapshot:

```text
c21184658d6d46d11a24c7c6fdfc08b106af1f87
```

The repository had no open PRs at this baseline.

Recently merged implementation work includes:

- PR #5: generic Modbus storage telemetry;
- PR #6: normalized HEMS state and constraint engine;
- PR #7: OTA factory-image upload guard;
- PR #8: EV protection policy in Shadow mode;
- PR #9: repository-level importable device-profile catalog;
- PR #10: `evse` capability for Modbus profile schema v1.

## Architecture currently implemented

```text
Devices / providers
    ↓
read-only telemetry
    ↓
normalized HEMS state
    ↓
constraint model
    ↓
Shadow policies
```

The intended later execution path remains:

```text
Devices / providers
    ↓
normalized telemetry
    ↓
HEMS state / constraints
    ↓
optimizer / policies
    ↓
Safety Supervisor
    ↓
actuators
```

Rule: one actuator, one boss. Existing DPL remains the fast physical regulation path and must not depend on slower HEMS network/policy work.

## Implemented

### HEMS core

- HEMS configuration and Web UI exist;
- runtime modes currently remain `Disabled` and `Monitor`;
- no global Active mode exists yet;
- `/api/hems/status` exposes diagnostics and normalized state.

### Modbus / providers

- bounded Modbus TCP read path exists;
- WARP integration remains read-only and uses its dedicated provider;
- generic profile-driven storage telemetry exists;
- missing, failed and stale telemetry is not fabricated as zero/false.

### Modbus profiles

Schema:

```text
opendtu-hems-modbus-profile/v1
```

Current profile properties:

- read-only;
- bounded parser/storage model;
- import/export support;
- `storage` and `evse` capabilities;
- no executable write/control/failsafe semantics in profile v1.

Repository profile catalog:

```text
device-profiles/
├── storage/
│   └── Marstek-Venus-E-3.0.json
└── evse/
    └── Tinkerforge-WARP3-Smart.json
```

These JSON files are deliberately **not built into firmware** and are **not installed automatically**. Users download/import the profile they need.

Current Marstek reference profile targets the observed Control V148 / Micro V119 setup. Important validated target semantics include:

```text
34002  SoC             uint16  × 0.01
30006  AC power        int16   × -1   (normalized HEMS sign)
30101  battery current int16   × 0.01
```

Public Venus E 3.0 mappings are not treated as universally interchangeable across firmware revisions.

### Normalized state / constraints

- Grid, PV, Storage and EVSE have normalized state;
- data quality distinguishes `unknown`, `fresh` and `stale`;
- fresh data is usable, stale data remains diagnostic-only;
- storage constraints support charge/discharge permission and optional power ceilings;
- constraints compose deterministically;
- current effective constraints remain neutral unless a later active policy installs restrictions.

### EV protection Shadow policy

Current behavior:

```text
fresh EVSE charging=true continuously for 120 s
    ↓
would_block_storage_discharge
```

This is Shadow-only:

- no storage write is issued;
- no EVSE write is issued;
- effective constraints remain neutral;
- stale/unknown/false charging resets the Shadow proposal.

The inactive/no-EVSE path has been validated on the reference ESP32-S3 hardware. The actual charging transition with a real WARP remains a later physical validation item.

### OTA factory-image guard

Merged behavior:

- file selection no longer starts upload automatically;
- explicit upload action is required;
- `.factory.bin` / `.factory.bin.gz` are rejected by the UI;
- backend independently rejects factory-image filenames before OTA writes begin;
- original filename is preserved in the multipart upload.

Dedicated on-device factory-file selection validation remains a useful hardware check, but the guard is already merged into `hems`.

## Known deferred validation

The following are deliberately not represented as completed hardware validation:

- real WARP3 charging/vehicle telemetry against the reference DTU;
- real WARP communication loss/recovery and packet-capture validation;
- W5500-to-real-WARP path where applicable;
- full live Marstek Venus E 3.0 telemetry validation through the imported repository profile;
- any storage write/control behavior;
- EV protection Active behavior.

## Reference hardware

Current staging/reference target:

```text
OpenDTU Fusion Board v2.1
Fusion Shield v1.1
ESP32-S3
8 MB flash
PIO environment: generic_esp32s3_usb
PSRAM not required for HEMS core functionality
```

Later target hardware includes a 16 MB / 8 MB PSRAM ESP32-S3 build, W5500, Hoymiles inverter, Shelly Pro 3EM, WARP3 Smart and Marstek Venus E 3.0.

## Not implemented yet

- generic profile-driven EVSE runtime consumer;
- migration/binding of imported WARP3 profiles to an EVSE consumer;
- storage write layer;
- manual storage control tests;
- Safety Supervisor for actuator writes;
- EV protection Active mode;
- dynamic tariff provider;
- economic optimizer Shadow mode;
- economic optimizer Active mode;
- PV forecast / forecast-aware policy.

## Working mode for future development

PRs remain the unit of work, but PR size is pragmatic rather than dogmatic.

The preferred workflow is:

1. identify the next coherent implementation chunk;
2. create/update a PR for that chunk;
3. explicitly state what is being put into that PR;
4. run deterministic CI/native validation automatically;
5. request hardware testing only at meaningful functional gates;
6. merge when the chunk is sufficiently validated;
7. continue with the next PR.

The implementation may use small PRs when they reduce risk or simplify review, but it may also use broader PRs when several tightly coupled changes are faster to validate together.

Avoid long stacked-PR chains unless there is a concrete benefit. Prefer one active implementation PR, with at most a short dependency stack when necessary.

For interactive development, communication should be explicit and sequential, for example:

```text
Ich kippe <konkreter Umbau> in PR #<n> ein.
```

Then that PR is completed/tested before moving to the next chunk unless parallel work is clearly useful.

## Likely next implementation order

This order is intentionally flexible and may be regrouped into fewer/larger PRs where useful:

1. generic profile-driven EVSE telemetry consumer and WARP3 profile binding;
2. storage write primitives and manual-control path, Marstek first;
3. write Safety Supervisor / execution gates;
4. EV protection Active execution;
5. dynamic tariff provider;
6. economic optimizer Shadow mode;
7. economic optimizer Active mode;
8. PV forecast integration.

Before the first real storage write, target-specific Marstek register semantics must be verified against the actual device/firmware rather than inferred from a single community source.
