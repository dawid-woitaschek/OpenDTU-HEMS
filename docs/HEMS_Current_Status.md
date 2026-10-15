# OpenDTU-HEMS current status

> Snapshot date: 2026-10-15
>
> This file is the short, authoritative hand-off for the current HEMS implementation state. `HEMS_Roadmap.md` remains the historical/forward plan and intentionally contains older milestone numbering in places.

## Current integration baseline

`integration/hems-next` currently points to:

```text
a77b16a599cfa7cba60c55f444864b6eca2543c4
```

That baseline includes the profile-driven telemetry/control-metadata foundation through PR #17. Current development work is intentionally split into a short dependency stack:

```text
integration/hems-next
└── #18  per-device HEMS control permission
    └── #20  guarded profile-driven Modbus write executor
        └── #21  semantic actuators + worker command bridge

#19 firmware update channels is independent of that control stack.
```

None of the control-stack PRs introduces a global Active mode.

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

The control branch additionally contains the still-fail-closed execution foundation:

```text
policy/controller caller (not connected yet)
    ↓
readiness / authorization gate
    ↓
depth-one semantic command mailbox
    ↓
existing per-device worker
    ↓
semantic actuator
    ↓
profile control plan
    ↓
bounded Modbus write + readback
```

Rule: one actuator, one boss. Telemetry and commands use the same per-device worker and Modbus client. Existing DPL remains the fast physical regulation path and must not depend on slower HEMS network/policy work.

## Integration baseline: implemented

### HEMS core

- HEMS configuration and Web UI exist;
- runtime modes remain `Disabled` and `Monitor`;
- no global Active mode exists;
- `/api/hems/status` exposes diagnostics, normalized state, constraints and Shadow-policy state.

### Modbus / providers

- bounded Modbus TCP read path exists;
- profile-driven generic storage telemetry exists;
- WARP telemetry can use the legacy mapping or an explicitly bound imported EVSE profile;
- missing, failed and stale telemetry is not fabricated as zero/false;
- provider work runs outside the fast scheduler path.

### Modbus profile schemas

Supported schemas:

```text
opendtu-hems-modbus-profile/v1  read-only
opendtu-hems-modbus-profile/v2  read telemetry + bounded declarative control metadata
```

Schema-v2 control metadata describes only allowed operations and holding-register writes. Merely importing such a profile never authorizes hardware writes.

Repository profile catalog:

```text
device-profiles/
├── storage/
│   └── Marstek-Venus-E-3.0.json
└── evse/
    └── Tinkerforge-WARP3-Smart.json
```

Profiles are deliberately not installed automatically. They remain explicit imported configuration.

### Curated WARP3 Smart profile

Verified Tinkerforge semantics used by the profile:

```text
input  1000/1001  IEC state             uint32
input  1002/1003  charging state        uint32
input  1010/1011  allowed current       uint32 mA
holding 1002/1003 Modbus current limit  uint32 mA
```

Control semantics on the current development branch are:

```text
setCurrent(6..32 A)  -> 6000..32000 mA
stop                  -> 0 mA
release               -> 32000 mA
```

Tinkerforge initializes the active Modbus charging slot with a 32000 mA maximum. `release` therefore restores that neutral/default slot cap without changing the charger's separate Modbus-enable configuration. Other charger slots, cable limits and safety logic continue to constrain actual current.

WARP3 Smart has no integrated meter, so the profile does not invent power/session-energy telemetry.

### Curated Marstek Venus E 3.0 profile

The current reference profile targets the observed V148/Micro V119 family and is intentionally conservative. Important mapped values include:

```text
34002  SoC                    uint16  × 0.1
30006  AC power               int16   × -1  (normalized HEMS sign)
30001  DC/battery power       int16   × 1
30100  battery voltage        uint16  × 0.01
30101  battery current        int16   × 0.1
33000  charged energy total   uint32  × 0.01
33002  discharged energy      uint32  × 0.01
35000  temperature            int16   × 0.1
35100  operating state        uint16
36000  alarm code             uint32
36100  fault code             uint32
```

Current declarative control metadata includes acquire/release, charge power, discharge power, stop and target SoC. Public Venus mappings are not treated as universally interchangeable across firmware revisions, and real write behavior is still a hardware-validation gate.

### Normalized state / constraints

- Grid, PV, Storage and EVSE have normalized state;
- data quality distinguishes `unknown`, `fresh` and `stale`;
- stale data remains diagnostic-only;
- storage constraints support charge/discharge permission and optional power ceilings;
- constraints compose deterministically;
- current effective constraints remain neutral because no Active policy is installed.

### EV protection Shadow policy

The development branch corrects the original debounce direction. Intended/current branch behavior is now:

```text
fresh EVSE charging=true
    -> immediately propose BlockDischarge

fresh EVSE charging=false
    -> keep BlockDischarge proposed for 120 s
    -> release only after a continuous fresh-false window

stale/unknown EVSE state
    -> never interpreted as "not charging"
    -> cannot silently release an already active protection state
```

The policy remains Shadow-only and does not write storage or EVSE hardware.

## Current control-stack development

### PR #18: per-device permission

Adds explicit per-device:

```text
Monitor only
HEMS control
```

Default and migration behavior are fail-closed at `Monitor only`. The permission alone cannot cause a write.

### PR #20: generic write executor

Adds bounded profile-driven holding-register execution:

- 16/32-bit control values;
- FC06/FC16 as appropriate;
- semantic-input range/step/scaling validation;
- ordered write plans;
- read-after-write verification;
- transport/readback/verification errors kept distinct.

### PR #21: semantic actuators and worker bridge

Adds semantic operations:

```text
Storage:
  setChargePower()
  setDischargePower()
  setTargetSoc()
  stop()
  releaseControl()

EVSE:
  setCurrent()
  stop()
  releaseControl()
```

The branch also contains:

- profile acquire/release sessions;
- a depth-one command mailbox;
- readiness diagnostics;
- generation-based invalidation when device configuration changes;
- last-moment authorization immediately before every bounded write;
- shared provider worker/client ownership for telemetry and control;
- fresh telemetry scheduling after a completed command.

`Controller::submitActuation()` is wired to the providers but production authorization still fails closed because the global Active prerequisite is hard false. There is no HTTP control endpoint and no automatic policy caller.

Before Active mode is introduced, lifecycle behavior still needs an explicit rule for relinquishing persistent remote control when Active is revoked or an endpoint/profile is changed.

## Firmware update-channel development

PR #19 adds repository-backed update channels with direct raw OTA binaries instead of requiring ZIP download/extraction.

Implemented concepts include:

```text
hems-next
hems
pr-<number>
```

A firmware image embeds its update channel and build environment. The firmware page resolves a manifest, selects the matching PIO environment, validates the image metadata/hash and sends the normal OTA binary through the existing update endpoint. Manual upload remains available as the fallback path.

The `pr-19` path and update-page current-version detection were successfully exercised on the reference Fusion v2 hardware with `generic_esp32s3_usb`. This is branch validation, not a claim that PR #19 is already part of the integration baseline.

## Known deferred physical validation

The following are deliberately not represented as completed hardware validation:

- real WARP3 telemetry across unplugged/connected/charging states using the imported profile;
- WARP current-limit/stop/release writes;
- WARP communication loss/recovery and packet-capture validation;
- W5500-to-real-WARP path where applicable;
- full live Marstek Venus E 3.0 telemetry validation through the curated profile;
- Marstek acquire/charge/discharge/stop/release behavior;
- persistent-command behavior across communication loss/reboot;
- any Active HEMS behavior.

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

## Still not implemented / not enabled

- global HEMS Active mode;
- automatic policy-to-actuator execution;
- public/manual diagnostic hardware-control API/UI;
- completed control-lifecycle cleanup on Active exit/config changes;
- dynamic tariff provider;
- economic optimizer Shadow mode;
- economic optimizer Active mode;
- PV forecast / forecast-aware policy.

## Likely next implementation order

1. finish control-lifecycle/deactivation semantics around the current command bridge;
2. expose useful readiness/command diagnostics without enabling writes;
3. prepare a deliberately gated manual-control validation path for real hardware;
4. validate WARP and Marstek on the actual devices;
5. only then introduce Active policy execution;
6. continue with tariff/optimizer/forecast work.

The project should continue to prefer explicit hardware-pending markers over pretending native tests validated a physical storage system. Electronics are sufficiently creative without documentation joining in.
