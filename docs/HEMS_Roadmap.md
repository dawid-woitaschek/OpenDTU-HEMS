# OpenDTU-HEMS Development Roadmap

> This is the living implementation tracker for OpenDTU-HEMS.
>
> The architecture and long-term design live in [`HEMS_Architecture.md`](HEMS_Architecture.md). This file tracks the current milestone, acceptance criteria and implementation order.

## Status legend

| Status | Meaning |
|---|---|
| ⚪ Planned | Scope defined, not started |
| 🔵 In development | Implementation work in progress |
| 🟡 Validation | Code exists; CI and/or hardware validation still pending |
| 🟢 Completed | Merged and validated |
| 🔴 Blocked | Cannot proceed until a dependency/problem is resolved |

## Current status

**Current milestone:** PR #2 - HEMS core, configuration model and Web UI skeleton

**Current phase:** 🔵 In development

PR #1 is merged into `hems` and validated on the reference device. The current work adds a disabled-by-default HEMS runtime, versioned configuration and the first settings/info pages without external integrations or hardware writes.

## Milestone overview

| Milestone | Scope | Status |
|---|---|---|
| PR #1 | HEMS CI build matrix and project baseline documentation | 🟢 Completed |
| PR #2 | HEMS core, configuration model and Web UI skeleton | 🔵 In development |
| PR #3 | Shared Modbus TCP layer and WARP telemetry | ⚪ Planned |
| PR #4 | Modbus profile engine and profile import/export | ⚪ Planned |
| PR #5 | Marstek Venus E telemetry | ⚪ Planned |
| PR #6 | Normalized HEMS state and constraint engine | ⚪ Planned |
| PR #7 | EV protection policy in Shadow mode | ⚪ Planned |
| PR #8 | Storage write layer and manual control tests | ⚪ Planned |
| PR #9 | EV protection policy in Active mode | ⚪ Planned |
| PR #10 | Dynamic tariff provider | ⚪ Planned |
| PR #11 | Economic optimizer in Shadow mode | ⚪ Planned |
| PR #12 | Economic optimizer in Active mode | ⚪ Planned |
| PR #13 | PV forecast provider and forecast-aware policy | ⚪ Planned |

The numbering above describes the intended implementation order. Actual GitHub pull-request numbers may differ if maintenance/fix PRs are required in between.

---

## PR #1 - HEMS build infrastructure

**Status:** 🟢 Completed

**Branch:** `ci/hems-build-matrix`  
**GitHub PR:** `#1`

### Goal

Create a dedicated OpenDTU-HEMS build pipeline that produces flashable firmware for representative ESP32 >=8 MB targets before runtime HEMS code is introduced.

### Scope

- dedicated OpenDTU-HEMS GitHub Actions workflow;
- WebApp build;
- PlatformIO build matrix;
- OTA firmware artifacts;
- factory firmware artifacts;
- reference Fusion v2 builds;
- generic >=8 MB ESP32/S3 builds;
- project architecture and roadmap documentation;
- no HEMS runtime behavior changes;
- no device writes.

### Build targets

```text
generic_esp32_8mb
generic_esp32_16mb_psram
generic_esp32s3
generic_esp32s3_usb
generic_esp32s3_usb_octal_psram
generic_esp32s3_usb_quad_psram
opendtufusionv2
opendtufusionv2_poe
```

### Acceptance criteria

- [x] dedicated HEMS workflow created;
- [x] generic ESP32 8 MB build succeeds;
- [x] generic ESP32 16 MB + PSRAM build succeeds;
- [x] ESP32-S3 builds succeed;
- [x] Fusion v2 build succeeds;
- [x] Fusion v2 PoE/W5500 build succeeds;
- [x] OTA binaries generated;
- [x] factory binaries generated;
- [x] upstream OpenDTU-OnBattery build succeeds;
- [x] upstream OpenDTU-OnBattery tests succeed;
- [x] architecture document added;
- [x] living roadmap added;
- [x] inherited/upstream artifact messaging reviewed (HEMS workflow and PR artifact index identify this fork);
- [x] baseline firmware flashed to reference Fusion v2;
- [x] configuration remains intact after OTA update;
- [x] Web UI smoke test passed;
- [x] LAN/Wi-Fi networking smoke test passed;
- [x] inverter communication smoke test passed;
- [x] configured power-meter integration smoke test passed;
- [x] DPL smoke test passed;
- [x] MQTT smoke test passed where configured;
- [x] reboot smoke test passed;
- [x] PR merged into `hems`;
- [x] post-merge smoke test completed.

### Reference hardware for validation

Current staging device:

```text
OpenDTU Fusion v2
ESP32-S3
2 cores @ 240 MHz
8 MB flash
current firmware environment: generic_esp32s3_usb
PSRAM physically available on the relevant board revision but not enabled in the current build
```

The baseline test deliberately uses `generic_esp32s3_usb` first because that matches the currently installed firmware environment. Enabling QSPI PSRAM later is treated as a separate hardware/build change rather than being mixed into PR #1 validation.

### PSRAM migration note

Planned later migration for the reference Fusion board:

```text
current:
generic_esp32s3_usb
    8 MB flash
    PSRAM not enabled

one-time factory flash over USB:
generic_esp32s3_usb_quad_psram.factory.bin
    8 MB flash
    2 MB QSPI PSRAM enabled

subsequent OTA updates:
generic_esp32s3_usb_quad_psram.bin
```

OpenDTU-HEMS core functionality must continue to work without PSRAM. PSRAM is an optional capability, not a hard dependency.

### Exit condition

PR #1 is complete only after the build pipeline is merged and a generated baseline OTA image has been successfully validated on the reference Fusion device.

---

## PR #2 - HEMS core and Web UI skeleton

**Status:** 🔵 In development

**Branch:** `feature/hems-core`

**Target:** `hems`

### Goal

Introduce the smallest possible real HEMS runtime skeleton without external device integrations.

### Planned scope

- HEMS core controller;
- only the base operating-mode type needed by this milestone;
- versioned HEMS configuration block;
- default state: disabled;
- operating modes initially exposed as `Disabled` and `Monitor` only;
- `/settings/hems` route;
- `/info/hems` route;
- navigation entries;
- API endpoints required by the Web UI;
- translations;
- basic status/diagnostics;
- no Modbus dependency;
- no storage integration;
- no EVSE integration;
- no hardware writes.

### Expected UI

Settings:

```text
HEMS enabled       [ ]
Operating mode     Monitor
```

Status:

```text
HEMS state         Disabled / Monitor
Decision           None
External devices   None configured
```

### Acceptance criteria

- [ ] legacy configuration loads without HEMS settings;
- [ ] HEMS defaults to disabled;
- [ ] HEMS config persists across reboot;
- [ ] Settings page works;
- [ ] Info page works;
- [ ] existing OpenDTU functionality remains unchanged while HEMS is disabled;
- [ ] CI green;
- [ ] reference Fusion smoke test passed;
- [ ] PR merged;
- [ ] post-merge smoke test passed.

---

## PR #3 - Shared Modbus TCP layer and WARP telemetry

**Status:** ⚪ Planned

### Goal

Add the first external HEMS device integration using a read-only WARP3 Modbus TCP provider.

### Planned scope

- select and validate an asynchronous Modbus TCP implementation;
- shared Modbus connection/request layer;
- WARP provider configuration;
- read-only WARP telemetry;
- reachability and data age;
- Web UI configuration and status;
- no WARP write functions.

### Initial WARP telemetry

- charging state;
- vehicle connected state where reliably derivable;
- current charging power;
- allowed charging current;
- session energy where useful;
- connection status;
- data age.

### Acceptance criteria

- [ ] Modbus layer builds on all official HEMS CI targets;
- [ ] W5500/Ethernet path verified where available;
- [ ] network timeout does not block DPL;
- [ ] WARP unreachable state handled correctly;
- [ ] stale WARP data becomes unknown rather than false/zero;
- [ ] no write request exists in the WARP provider;
- [ ] CI green;
- [ ] real WARP telemetry validated;
- [ ] PR merged;
- [ ] post-merge smoke test passed.

---

## PR #4 - Modbus profile engine

**Status:** ⚪ Planned

### Goal

Create a reusable, vendor-neutral register profile mechanism for storage and future Modbus devices.

### Planned scope

- `opendtu-hems-modbus-profile/v1` schema;
- profile metadata and provenance;
- transport defaults;
- telemetry definitions;
- semantic storage mode definitions;
- validation;
- built-in profile registry;
- import/export;
- LittleFS persistence where appropriate;
- read-only test functionality;
- imported profiles do not enable writes automatically.

### Tinkerforge-derived profiles

Evaluate a build-time converter for definitions from:

`Tinkerforge/esp32-firmware/software/src/modules/batteries_modbus_tcp`

Requirements:

- no runtime Python execution on the ESP32;
- pin source repository and source revision;
- preserve attribution/licensing requirements;
- validate generated profiles against the OpenDTU-HEMS schema.

### Acceptance criteria

- [ ] malformed profiles rejected;
- [ ] imported profile cannot silently activate writes;
- [ ] profile size bounded;
- [ ] profile schema version handled explicitly;
- [ ] source/provenance visible in diagnostics/UI;
- [ ] CI green;
- [ ] reference hardware import test passed;
- [ ] PR merged.

---

## PR #5 - Marstek Venus E telemetry

**Status:** ⚪ Planned

### Goal

Integrate the first AC-coupled storage system read-only.

### Planned scope

- verify the actual Venus E register map;
- create first Marstek profile/provider;
- SoC telemetry;
- charge/discharge power telemetry;
- operating/fault state where available;
- data age and communication status;
- storage Web UI;
- no write operations.

### Acceptance criteria

- [ ] all used registers have documented provenance;
- [ ] no guessed or inferred write registers;
- [ ] telemetry sign convention verified;
- [ ] long-running read-only polling stable;
- [ ] communication loss handled safely;
- [ ] CI green;
- [ ] real Venus E telemetry validated;
- [ ] PR merged.

---

## PR #6 - Normalized HEMS state and constraint engine

**Status:** ⚪ Planned

### Goal

Separate provider I/O from policy logic through a normalized state store and deterministic constraints.

### Planned scope

- normalized Grid/Storage/EVSE state;
- validity and data age;
- provider-independent snapshots;
- constraint model;
- initial policy priority system;
- diagnostics for stale and invalid inputs;
- still no automated writes.

### Acceptance criteria

- [ ] policies do not perform blocking provider I/O;
- [ ] stale data is explicit;
- [ ] conflicting constraints resolve deterministically;
- [ ] diagnostics show decision inputs;
- [ ] CI green;
- [ ] reference hardware smoke test passed;
- [ ] PR merged.

---

## PR #7 - EV protection policy in Shadow mode

**Status:** ⚪ Planned

### Goal

Implement the first real HEMS decision without controlling hardware.

### Initial rule

```text
IF
    WARP data is valid
AND WARP is actively charging
AND charging power > configured threshold
THEN
    desired storage mode = BlockDischarge
```

A debounce period should prevent rapid mode oscillation after short charging interruptions.

### Planned scope

- `Shadow` global operating mode;
- EV protection policy;
- configurable active-power threshold;
- configurable stop debounce;
- decision reason;
- winning policy display;
- `WOULD_BLOCK_DISCHARGE` diagnostics;
- no hardware write.

### Acceptance criteria

- [ ] multiple real EV charging sessions observed;
- [ ] charging start detected correctly;
- [ ] short charging pauses do not cause flapping;
- [ ] WARP communication loss does not become `not charging`;
- [ ] Shadow mode never writes storage registers;
- [ ] CI green;
- [ ] PR merged.

---

## PR #8 - Storage write layer and manual control tests

**Status:** ⚪ Planned

### Goal

Add storage control capabilities without yet connecting them to automatic HEMS policies.

### Planned scope

- semantic storage commands;
- per-device `Allow external control` switch;
- Safety Supervisor checks;
- manual diagnostic controls;
- write rate limiting;
- readback/verification where supported;
- explicit restore-to-normal operation;
- still no automatic EV policy execution.

### Acceptance criteria

- [ ] external control disabled by default;
- [ ] imported profile does not enable writes;
- [ ] manual `Normal` verified;
- [ ] manual `BlockDischarge` verified;
- [ ] restore-to-normal verified;
- [ ] communication loss behavior tested;
- [ ] reboot behavior tested;
- [ ] persistent command behavior documented;
- [ ] CI green;
- [ ] PR merged.

---

## PR #9 - EV protection policy in Active mode

**Status:** ⚪ Planned

### Goal

Connect the validated EV protection policy to the validated storage write layer.

### Planned scope

- `Active` HEMS mode;
- automatic BlockDischarge while EV charging;
- safe return to Normal after debounce;
- decision/write logging;
- per-device external-control requirement;
- communication-loss fallback.

### Acceptance criteria

- [ ] Shadow behavior already proven;
- [ ] WARP remains read-only;
- [ ] storage does not discharge into EV during active policy;
- [ ] storage returns to normal correctly;
- [ ] DTU reboot behavior tested;
- [ ] WARP outage tested;
- [ ] storage outage tested;
- [ ] CI green;
- [ ] PR merged.

---

## PR #10 - Dynamic tariff provider

**Status:** ⚪ Planned

### Goal

Add tariff ingestion and visualization without active economic control.

### Planned scope

- tariff provider abstraction;
- 15-minute slot model where supported;
- effective end-customer import price;
- current/future price display;
- validity/data age;
- provider secrets handling;
- no automatic storage control.

### Acceptance criteria

- [ ] provider outage handled gracefully;
- [ ] expired tariff data becomes invalid;
- [ ] secrets masked;
- [ ] price time zones/DST validated;
- [ ] CI green;
- [ ] PR merged.

---

## PR #11 - Economic optimizer in Shadow mode

**Status:** ⚪ Planned

### Goal

Generate an economically useful storage plan without executing it.

### Planned inputs

- tariff slots;
- current SoC;
- usable storage capacity;
- minimum and maximum SoC;
- charge/discharge power limits;
- round-trip efficiency;
- optional battery cycle cost.

### Planned output

A time-slot plan containing semantic decisions such as:

```text
Normal
BlockDischarge
ForceCharge
```

### Acceptance criteria

- [ ] optimizer accounts for round-trip losses;
- [ ] multiple time slots considered;
- [ ] capacity/power constraints respected;
- [ ] result explainable in UI;
- [ ] no hardware writes in Shadow mode;
- [ ] several days of real tariff data reviewed;
- [ ] CI green;
- [ ] PR merged.

---

## PR #12 - Economic optimizer in Active mode

**Status:** ⚪ Planned

### Goal

Allow the validated economic plan to control a supported storage system under Safety Supervisor constraints.

### Acceptance criteria

- [ ] Shadow results previously validated;
- [ ] external control explicitly enabled;
- [ ] invalid tariff data suppresses economic writes;
- [ ] storage safety/SoC limits override optimizer;
- [ ] persistent commands handled safely;
- [ ] reboot behavior tested;
- [ ] CI green;
- [ ] PR merged.

---

## PR #13 - PV forecast integration

**Status:** ⚪ Planned

### Goal

Use expected PV generation to improve storage scheduling.

### Planned scope

- forecast provider abstraction;
- forecast validity/data age;
- UI visualization;
- optimizer input;
- reserve free storage capacity when large PV surplus is expected;
- avoid unnecessary grid charging before sunny periods.

### Acceptance criteria

- [ ] HEMS remains functional when forecast is unavailable;
- [ ] forecast cannot override hard safety constraints;
- [ ] decisions explain forecast influence;
- [ ] CI green;
- [ ] PR merged.

---

## Working rule for future milestones

Only one main implementation milestone is actively developed at a time.

Workflow:

```text
Start PR N
    -> mark PR N as 🔵 In development
    -> implement
    -> CI
    -> fix within same PR
    -> mark 🟡 Validation
    -> hardware validation
    -> merge
    -> post-merge smoke test

Start PR N+1
    -> mark PR N as 🟢 Completed
    -> mark PR N+1 as 🔵 In development
```

Small corrective commits remain in the current PR whenever they belong to that milestone. A new PR is not opened merely to avoid fixing the current one.

## Documentation rule

Every implementation PR should update this roadmap when its state changes.

The roadmap should describe what has actually been validated, not what merely exists in a branch.

Architecture changes that affect long-term design should additionally update [`HEMS_Architecture.md`](HEMS_Architecture.md).
