# OpenDTU-HEMS current status

> Snapshot date: 2026-10-15
>
> This file is the short implementation hand-off. The authoritative implementation order and milestone acceptance criteria live in [`HEMS_Roadmap.md`](HEMS_Roadmap.md).

## Current branch topology

Shared integration base:

```text
integration/hems-next
1724b5d459ec9068333b3ace178468dedbf4aaea
```

That baseline includes the firmware-update infrastructure from merged PR #19.

Current development:

```text
integration/hems-next
└── PR #22  feature/hems-actuation-foundation
```

PRs #18, #20 and #21 are closed without merge and were consolidated into PR #22. Do not treat them as active dependencies.

At this document refresh, #22 contains one consolidated functional commit plus documentation updates. No global HEMS Active mode exists.

## Architecture currently implemented

Read/decision path:

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

Fail-closed control foundation in PR #22:

```text
policy/controller caller (automatic caller not connected)
    ↓
readiness / authorization gate
    ↓
depth-one semantic command slot
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

## Integration baseline capabilities

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

```text
opendtu-hems-modbus-profile/v1  read-only
opendtu-hems-modbus-profile/v2  telemetry + bounded declarative control metadata
```

Schema-v2 metadata describes allowed semantic operations and bounded holding-register writes. Importing such a profile never authorizes hardware writes.

Repository catalog:

```text
device-profiles/
├── storage/
│   └── Marstek-Venus-E-3.0.json
└── evse/
    └── Tinkerforge-WARP3-Smart.json
```

Profiles are explicit imported configuration and are not automatically installed or activated.

## Curated WARP3 Smart semantics

Verified register semantics currently represented by the profile:

```text
input   1000/1001  IEC state             uint32
input   1002/1003  charging state        uint32
input   1010/1011  allowed current       uint32 mA
holding 1002/1003  Modbus current limit  uint32 mA
```

Control semantics in PR #22:

```text
setCurrent(6..32 A)  -> 6000..32000 mA
stop                  -> 0 mA
release               -> 32000 mA
```

`release` restores the neutral/default Modbus charging-slot cap, not an unconditional request to charge at 32 A. Other WARP slots, cable limits and safety logic remain authoritative.

WARP3 Smart has no integrated meter, so the profile does not invent power/session-energy telemetry.

## Curated Marstek Venus E 3.0 semantics

The current reference profile targets the observed V148/Micro V119 family and remains deliberately conservative.

Important telemetry includes:

```text
34002  SoC                    uint16  × 0.1
30006  AC power               int16   normalized HEMS sign
30001  DC/battery power       int16
30100  battery voltage        uint16  × 0.01
30101  battery current        int16   × 0.1
33000  charged energy total   uint32  × 0.01
33002  discharged energy      uint32  × 0.01
35000  temperature            int16   × 0.1
35100  operating state        uint16
36000  alarm code             uint32
36100  fault code             uint32
```

Declarative control metadata includes acquire/release, charge power, discharge power, stop and target SoC.

Public Venus register maps are not assumed interchangeable across firmware revisions. Real write behavior remains a hardware-validation gate.

## Normalized state / constraints

- Grid, PV, Storage and EVSE have normalized state;
- quality distinguishes `unknown`, `fresh` and `stale`;
- stale data is diagnostic-only;
- storage constraints support charge/discharge permission and optional power ceilings;
- constraints compose deterministically;
- current effective production constraints remain neutral because no Active policy is installed.

## EV protection Shadow policy

PR #22 contains the corrected debounce behavior:

```text
fresh charging=true
    -> propose BlockDischarge immediately

fresh charging=false
    -> retain proposal for 120 s continuous fresh-false

charging resumes inside the window
    -> protection remains/restores immediately

stale/unknown
    -> never interpreted as not charging
    -> cannot silently release an existing protection state
```

The policy remains Shadow-only.

## PR #22: actuation foundation

### Per-device permission

Each EVSE/storage consumer can persist:

```text
Monitor only
HEMS control
```

Missing/legacy settings default fail-closed to Monitor.

### Generic write executor

- semantic operation to bounded `ControlPlan`;
- input min/max/step/scaling checks;
- `uint16`, `int16`, `uint32`, `int32` writes;
- FC06 for one-register writes;
- FC16 for two-register scalar writes;
- ordered execution;
- optional read-after-write verification;
- separate transport/readback/verification failures.

### Semantic actuators

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

### Command bridge and safety

- depth-one semantic command slot;
- pending/running work cannot be overwritten;
- commands are pinned to the provider generation that accepted them;
- authorization is checked again immediately before bounded writes;
- telemetry and commands share the same worker/client;
- completed commands schedule fresh telemetry;
- there is no public normal-operation write endpoint;
- there is no automatic policy caller;
- global Active authorization remains absent/fail-closed.

### Ownership cleanup

A live endpoint/profile configuration change no longer silently forgets possible remote ownership.

The provider retains the exact old endpoint/effective Modbus settings/profile and blocks normal actuation while cleanup is pending.

Cleanup:

- runs through the same provider worker/client;
- may bypass normal Active/device-permission only for the retained old profile's bounded release sequence;
- never retargets release to the new endpoint/profile;
- retries failures with bounded delay;
- clears retained context only after successful release;
- is exposed in diagnostics.

Remaining lifecycle hole: retained cleanup is RAM-resident. A hard reboot/power loss can erase ownership knowledge. Boot/power-loss recovery is the next safety milestone before Active mode.

## Firmware update channels

PR #19 is merged into `integration/hems-next`.

Persistent channels include:

```text
hems-next
hems
```

Same-repository pull requests can publish temporary:

```text
pr-<number>
```

Generated channel branches contain a manifest and raw normal OTA binaries per target. Factory images stay out of automatic-update channels. Manual OTA upload remains available as fallback.

The reference hardware currently uses:

```text
PIO environment: generic_esp32s3_usb
```

## Physical validation still open

### WARP3

- real profile telemetry for unplugged/connected/charging states;
- allowed-current behavior;
- `setCurrent()` writes;
- stop/release writes;
- communication loss/recovery;
- reboot behavior while a limit may be active;
- W5500 path where applicable.

### Marstek Venus E 3.0

- full curated telemetry plausibility;
- acquire;
- charge/discharge power;
- stop;
- target SoC;
- release;
- communication loss while owned;
- reboot/power-loss recovery;
- validation against actual device/firmware register behavior.

No physical write behavior should be described as completed until exercised on the real device.

## Immediate next order

```text
1. validate and merge PR #22
2. implement boot/power-loss ownership recovery
3. add deliberately gated semantic hardware-validation path
4. validate WARP3 and Marstek on real hardware
5. introduce global Active + arbitration
6. make EV protection the first Active policy
7. add dynamic tariff provider
8. add economic optimizer in Shadow mode
9. activate economic optimizer after validation
10. add PV forecast / forecast-aware optimization
```

## Separate open investigation

An intermittent issue has been observed where existing connections to the reference DTU may continue while new inbound TCP connections time out. It is not currently proven to originate in HEMS changes.

Continue this separately with LAN/WAN path isolation and, if necessary, lwIP/AsyncTCP accept/PCB instrumentation. Do not silently convert correlation with HEMS development into causation.

## Reference hardware

```text
OpenDTU Fusion Board v2.1
Fusion Shield v1.1
ESP32-S3
8 MB flash
PIO environment: generic_esp32s3_usb
PSRAM not required for HEMS core functionality
```

## Handoff rule

When the active milestone, branch topology, integration baseline, safety boundary, physical validation result or implementation order changes, update both this file and `HEMS_Roadmap.md` in the same work block.
