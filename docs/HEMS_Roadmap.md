# OpenDTU-HEMS Development Roadmap

> This is the authoritative implementation tracker for OpenDTU-HEMS.
>
> Update this file whenever the active milestone, branch structure, validation state, or implementation order changes. It must be sufficient to resume work after losing conversational context.
>
> Long-term architecture lives in [`HEMS_Architecture.md`](HEMS_Architecture.md). Detailed current implementation facts live in [`HEMS_Current_Status.md`](HEMS_Current_Status.md).

## Status legend

| Status | Meaning |
|---|---|
| ⚪ Planned | Scope defined, not started |
| 🔵 In development | Implementation work in progress |
| 🟡 Validation | Code exists; CI and/or hardware validation still pending |
| 🟢 Completed | Merged and sufficiently validated for this milestone |
| 🔴 Blocked | Cannot proceed until a dependency/problem is resolved |

## Current project position

**Snapshot:** 2026-10-15

**Integration branch:** `integration/hems-next`

**Integration baseline:** `1724b5d459ec9068333b3ace178468dedbf4aaea`

That baseline includes the repository-backed firmware update infrastructure from merged PR #19.

**Current milestone:** PR #22, HEMS Actuation Foundation

**Current branch:** `feature/hems-actuation-foundation`

**Recovery work input head:** `9a5341c617fbd36461dd69ac22f70d2d6613f43f` (live #22 head checked before implementation; HEMS build/tests succeeded)

**Current phase:** 🟡 Validation — durable recovery and temporary validation-session foundation in #22

Branch structure is intentionally flat:

```text
integration/hems-next
└── #22 feature/hems-actuation-foundation
```

PRs #18, #20 and #21 were closed without merge and consolidated into #22. Their functionality is preserved in one commit directly on the shared integration base. Do not rebuild a stacked PR chain for this work.

## Non-negotiable architecture rules

- One actuator, one boss.
- Existing DPL remains the fast physical regulation path and must not depend on slower HEMS network/policy work.
- Telemetry and commands for one device use the same provider worker and Modbus client.
- Missing/stale/failed telemetry is never fabricated as zero or false.
- Importing a control-capable profile never grants permission to write.
- Per-device `HEMS control` permission alone never grants permission to write.
- Production writes require the global Active gate, runtime readiness and safety/arbitration.
- No unrestricted raw-register write API is planned for normal operation.
- Hardware validation is not implied by native tests.

## Capability overview

| Area | Status | Notes |
|---|---|---|
| Build/CI infrastructure | 🟢 Completed | Dedicated HEMS build matrix and OTA artifacts exist |
| HEMS core/UI | 🟢 Completed | Disabled/Monitor, config, status and UI |
| Shared Modbus TCP read path | 🟢 Implemented / 🟡 physical validation | Real WARP/W5500 validation remains incomplete |
| Modbus profile engine v1 | 🟢 Completed | Read-only profiles, import/export/persistence |
| Generic storage telemetry | 🟢 Implemented / 🟡 physical validation | Manufacturer-neutral provider exists |
| Normalized state + constraints | 🟢 Completed | Grid/Storage/EVSE normalization and quality semantics |
| EV protection Shadow | 🟢 Implemented | Corrected debounce semantics are in #22 until merged |
| Device profile catalog | 🟢 Completed | WARP3 Smart and Marstek Venus E 3.0 reference profiles |
| Profile schema v2 control metadata | 🟢 Completed | Declarative bounded control metadata only |
| Firmware update channels | 🟢 Completed | PR #19 merged into `integration/hems-next` |
| Actuation foundation | 🟡 Validation | PR #22 |
| Durable reboot awareness (B1) | 🟡 Validation | Crash-consistent per-target journal; unresolved recovery blocks commands |
| Generic automatic recovery (B2) | 🟡 Validation | Exact identity and replay-safe release required; synthetic native coverage |
| Reference-device identity (B3) | 🔵 Partial | WARP eFuse UID provenance established; Marstek unsupported |
| Real automatic reboot recovery (B4) | 🔴 Blocked | Both reference profiles retain replay-safe=false; physical validation outstanding |
| Temporary validation sessions (C1) | 🟡 Validation | Admin API/lifecycle UI; real commands blocked by recovery readiness |
| Real semantic hardware validation (C2) | ⚪ Planned | WARP + Marstek hardware gate |
| Global HEMS Active mode | ⚪ Planned | Must wait for lifecycle + hardware validation |
| EV protection Active | ⚪ Planned | First automatic control policy |
| Dynamic tariff provider | ⚪ Planned | No active economic control initially |
| Economic optimizer Shadow | ⚪ Planned | Plan only, no writes |
| Economic optimizer Active | ⚪ Planned | Only after Shadow validation |
| PV forecast integration | ⚪ Planned | Forecast-aware optimization |

## Completed foundation

The implemented read/decision path is:

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

Supported profile schemas:

```text
opendtu-hems-modbus-profile/v1
    read-only telemetry

opendtu-hems-modbus-profile/v2
    telemetry + bounded declarative control metadata
```

Reference profiles currently maintained in the repository:

```text
device-profiles/
├── evse/
│   └── Tinkerforge-WARP3-Smart.json
└── storage/
    └── Marstek-Venus-E-3.0.json
```

Profiles remain explicit configuration and are not silently installed or activated.

## Milestone A: PR #22, HEMS Actuation Foundation

**Status:** 🟡 Validation

**GitHub PR:** #22

**Branch:** `feature/hems-actuation-foundation`

### Goal

Complete the generic, fail-closed execution foundation required before any HEMS policy is allowed to control real hardware.

### Implemented scope

Per-device control permission:

```text
Monitor only
HEMS control
```

Default and migration behavior remain fail-closed at `Monitor only`.

Generic profile-driven control execution:

- semantic operation to bounded `ControlPlan` conversion;
- semantic input range/step/scaling validation;
- 16/32-bit holding-register values;
- FC06/FC16 write transport;
- ordered write sequences;
- read-after-write verification where configured;
- distinct transport/readback/verification failures.

Semantic actuator API:

```text
Storage:
  setChargePower(watts)
  setDischargePower(watts)
  setTargetSoc(percent)
  stop()
  releaseControl()

EVSE:
  setCurrent(amps)
  stop()
  releaseControl()
```

Worker/authorization/lifecycle foundation:

- depth-one semantic command slot;
- no overwrite of pending/running work;
- provider-generation pinning;
- authorization rechecked immediately before bounded writes;
- shared telemetry/control worker and Modbus client;
- profile acquire/release sessions;
- retained old endpoint/profile cleanup after configuration changes;
- bounded release retry;
- cleanup diagnostics and blockers;
- fresh telemetry scheduling after commands;
- actuation/readiness state exposed through `/api/hems/status`.

EV protection Shadow correction:

```text
fresh charging=true
    -> BlockDischarge proposed immediately

fresh charging=false
    -> protection remains for 120 s continuous fresh-false

charging resumes inside window
    -> protection immediately remains/restores

stale/unknown
    -> never interpreted as not charging
```

### WARP3 control semantics currently encoded

```text
setCurrent(6..32 A) -> 6000..32000 mA at holding 1002/1003
stop                -> 0 mA
release             -> 32000 mA neutral/default Modbus slot cap
```

### Safety boundary

PR #22 must remain unable to perform production actuation automatically:

- no global Active mode;
- no automatic policy caller;
- no public normal-operation write endpoint;
- global authorization remains fail-closed.

### Exit criteria

- [x] #18/#20/#21 functionality consolidated into one PR directly on `integration/hems-next`;
- [x] firmware update infrastructure lives in the shared integration base rather than a parallel dependency;
- [x] semantic actuator and executor native coverage exists;
- [x] command generation pinning exists;
- [x] retained cleanup exists for configuration changes while ownership may be active;
- [x] readiness/command/cleanup diagnostics exist;
- [ ] current #22 CI/build validation complete;
- [ ] review resulting `pr-22` firmware manifest/image for `generic_esp32s3_usb`;
- [ ] reference Fusion smoke test if firmware-level changes require it;
- [ ] merge #22 into `integration/hems-next`;
- [ ] update this roadmap and `HEMS_Current_Status.md` immediately after merge.

### Targeted audit corrections (pending CI/hardware validation)

- Failed acquire responses/readback retain possible ownership and reject further normal operations until successful release.
- Control requires a non-empty, plannable bounded release sequence; empty release no longer reports successful cleanup.
- Provider authorization rechecks generation after the external callback returns.
- Command completion is published after ownership/cleanup state, preventing the scheduler from dispatching work that command completion could then erase.
- Cleanup logging uses a locked counter snapshot; retry delay starts at failure completion, including slow failed attempts.
- Native regression coverage exercises both production providers with pending/running configuration changes, old endpoint/profile pinning, failed writes, retry delays and release/unblocking. No physical validation is claimed.
- HEMS uses small DNS callbacks and worker-side Modbus I/O; no demonstrated cause for TCP/IP stack pressure, heap leakage or the client-dependent remote-access issue was found. No task stack sizing changed.

## Milestone B: durable ownership awareness and identity-verified recovery

**Status:** 🟡 B1/B2 implemented, validation pending; 🔴 real automatic recovery remains blocked

| Layer | State | Scope |
|---|---|---|
| B1 crash-consistent ownership awareness | Implemented in #22 | Per-target LittleFS journal, durable prepare before ownership writes, durable clear only after release, fail-closed boot/corruption/IO handling |
| B2 generic identity/recovery framework | Implemented in #22 | Bounded read-only identities, exact recovery compatibility, replay-safe opt-in, shared worker/client/release executor, bounded retries |
| B3 real-device identity verification | Partial | WARP input 10/11 traces to provisioned eFuse UID; Marstek has no authoritative validated identity mapping |
| B4 real automatic reboot recovery | Blocked | Both reference releases remain replay-safe=false; actual identity/replay/reboot behavior must be physically validated |

The initial blocker was configuration tuples being mistaken for physical identity. B1 now safely remembers possible ownership without that assumption. Host/port/unit/profile matching alone never authorizes reboot writes. Profile compatibility pins exact release/identity semantics; it is not device identity.

Implemented transitions:

- validated bounded command/release -> identity read if declared -> durable journal -> acquire/ownership write;
- failed/unverifiable write -> intent retained;
- live config change -> old retained release context + same durable intent;
- boot with intent -> pending -> identity checks/release only if every prerequisite holds;
- unsupported/corrupt/incomplete journal, missing/incompatible profile, endpoint/identity mismatch -> unresolved, no write;
- release/readback -> local ownership released -> atomic durable tombstone;
- clear failure -> blocked flash-only retry, never replay an unvalidated release merely to retry clearing;
- retry delays start after attempt completion, at most five automatic attempts, 30 seconds between failed attempts;
- exact-record admin attestation of independent physical/vendor-interface release -> worker-only durable clear, no Modbus write.

Any interrupted pending file is evidence, including zero bytes. Filesystem mount failure no longer triggers automatic formatting. Downgrading/erasing the filesystem while remote ownership is possible is outside the safe protocol.

The WARP identity provenance is pinned in `HEMS_CONTROL.md`. Neither reference profile claims replay eligibility or physical reboot validation. Marstek identity remains explicitly unsupported. Corrupt/unsupported records cannot use operator acknowledgement because their device context cannot be trusted.

### Exit criteria

- [x] durable prepare-before-write and release-before-clear ordering;
- [x] corruption/truncation/version/endpoint/profile/identity failures remain blocked;
- [x] readiness and `/api/hems/status` recovery diagnostics;
- [x] synthetic identity/replay policy and production provider regression tests;
- [x] worker-based authenticated exact-record operator protocol;
- [ ] final #22 firmware/native CI succeeds;
- [ ] LittleFS power-cut behavior verified on reference hardware;
- [ ] Marstek stable physical identity established from authoritative source;
- [ ] reference release replay semantics and reboot behavior physically verified;
- [ ] real automatic recovery enabled only after its profile/hardware gate is satisfied.

## Milestone C: gated manual hardware validation path

**Status:** 🟡 C1 implemented / validation pending; ⚪ C2 real semantic validation not performed

C1 provides authenticated temporary sessions (default 120 seconds, maximum 300), RAM-only 128-bit random tokens, a depth-one production command handoff and a one-second session command rate limit. The session never enables global Active. The common readiness model still requires device permission, fresh telemetry, valid profile, no cleanup/recovery blocker, and automatic-recovery capability. Both real reference profiles currently fail that last gate.

Expiry/revocation/config changes revoke subsequent writes immediately and request the existing retained cleanup path. Running-command completion retains ownership created concurrently with revocation. Expiry causes no periodic flash writes when there is no ownership.

The settings UI can start/revoke a session and shows countdown, per-device profile, readiness, recovery and command/cleanup results. Semantic command testing uses the authenticated maintenance API; there are no raw register controls or generic journal-delete controls. Real command controls remain deferred while the reference recovery gate is blocked.

### Exit criteria

- [x] explicit temporary admin session and rate limiting;
- [x] same provider mailbox/worker/planner/executor/journal path;
- [x] common readiness blocks real commands while recovery support is unavailable;
- [x] expiry/revocation requests safe release including running-command races;
- [x] token/TTL/rate/revocation and production-provider cleanup native regressions;
- [ ] authenticated API and lifecycle UI verified on reference firmware;
- [ ] reference devices pass B3/B4 gates before real ownership-producing commands;
- [ ] actual bounded EVSE/storage commands, stop and release physically validated;
- [ ] real reboot/power-loss validation; no physical validation is claimed by native tests.

## Milestone D: physical WARP3 and Marstek validation

**Status:** ⚪ Planned

### WARP3 validation still required

- [ ] imported profile telemetry while unplugged;
- [ ] vehicle connected state;
- [ ] charging state;
- [ ] allowed-current telemetry plausibility;
- [ ] current limit 6 A / 10 A behavior;
- [ ] stop behavior;
- [ ] release behavior;
- [ ] communication loss/recovery;
- [ ] reboot behavior while a limit is/was active;
- [ ] W5500 path where applicable;
- [ ] packet/wire behavior checked when useful.

### Marstek Venus E 3.0 validation still required

- [ ] SoC/voltage/current/power/temperature plausibility;
- [ ] energy totals plausibility;
- [ ] state/alarm/fault semantics;
- [ ] acquire behavior;
- [ ] charge power behavior;
- [ ] discharge power behavior;
- [ ] stop behavior;
- [ ] target-SoC behavior;
- [ ] release behavior;
- [ ] communication loss during ownership;
- [ ] reboot/power-loss recovery;
- [ ] firmware/register-map assumptions recorded against tested device version.

Real-device validation may discover profile corrections. Such corrections belong in the same validation milestone and must update provenance/tests/documentation.

## Milestone E: global Active mode and arbitration

**Status:** ⚪ Planned

### Goal

Introduce the first production authorization layer that can permit actuator execution after the lower layers have been physically validated.

### Required scope

- explicit `Active` HEMS mode;
- global authorization gate;
- deterministic arbitration of policy intents;
- hard safety/readiness constraints override optimization;
- unresolved cleanup/recovery blocks actuation;
- stale/unknown required telemetry suppresses unsafe writes;
- decision reason visible in status/UI;
- transition Active -> Monitor/Disabled performs required cleanup/release;
- no coupling of slow HEMS policy work into the fast DPL loop.

Active must not be merged merely because the write primitives exist.

## Milestone F: EV protection in Active mode

**Status:** ⚪ Planned

### Goal

Make the already-developed EV protection Shadow policy the first automatic production policy.

### Intended behavior

```text
EV charging
    -> block storage discharge immediately

EV charging stops
    -> retain block for 120 s continuous fresh-false

EVSE stale/unknown
    -> do not infer safe release
```

### Exit criteria

- [ ] Shadow decisions match expected real EV sessions first;
- [ ] automatic storage protection uses the common arbitration/actuator path;
- [ ] communication loss remains fail-safe;
- [ ] Active exit releases persistent external control safely;
- [ ] reboot/recovery tested;
- [ ] no DPL regression.

## Milestone G: dynamic tariff provider

**Status:** ⚪ Planned

### Goal

Ingest and visualize dynamic electricity prices without automatically controlling storage.

### Scope

- provider abstraction;
- current and future tariff slots;
- effective end-customer import price;
- validity/data age;
- timezone/DST handling;
- provider secrets handling;
- UI visualization;
- no economic hardware writes.

### Exit criteria

- [ ] provider outage handled gracefully;
- [ ] expired tariff data invalidated;
- [ ] secrets masked;
- [ ] DST/time-zone behavior validated;
- [ ] several days of real data reviewed.

## Milestone H: economic optimizer in Shadow mode

**Status:** ⚪ Planned

### Goal

Generate an economically useful storage schedule without executing it.

### Planned inputs

- tariff slots;
- current SoC;
- usable capacity;
- minimum/maximum SoC;
- charge/discharge power limits;
- efficiency;
- optional battery cycle cost;
- later PV forecast input.

### Output

A time-slot plan with explainable semantic intents, for example:

```text
Normal
BlockDischarge
ForceCharge
ForceDischarge (only if explicitly allowed by product design)
```

### Exit criteria

- [ ] round-trip losses represented;
- [ ] capacity/power constraints respected;
- [ ] multiple time slots optimized;
- [ ] decisions explainable in UI;
- [ ] no writes in Shadow mode;
- [ ] real tariff days reviewed against expected behavior.

## Milestone I: economic optimizer in Active mode

**Status:** ⚪ Planned

### Goal

Allow a validated economic plan to drive supported storage under the common Safety Supervisor/arbitration rules.

### Exit criteria

- [ ] Shadow behavior previously accepted;
- [ ] Active mode explicitly enabled;
- [ ] invalid tariff data suppresses economic writes;
- [ ] SoC/device constraints override economics;
- [ ] EV protection overrides conflicting economic discharge;
- [ ] persistent commands/recovery handled safely;
- [ ] reboot and provider-outage behavior tested.

## Milestone J: PV forecast and forecast-aware optimization

**Status:** ⚪ Planned

### Goal

Use expected PV generation to improve storage scheduling.

### Scope

- forecast provider abstraction;
- validity/data age;
- UI visualization;
- optimizer input;
- reserve storage capacity for expected PV surplus;
- avoid unnecessary grid charging before sunny periods.

### Exit criteria

- [ ] HEMS remains functional without forecast data;
- [ ] forecast never overrides hard safety constraints;
- [ ] decisions explain forecast influence;
- [ ] forecast error does not create unsafe actuation.

## Deferred / separate investigation

An intermittent condition has been observed where existing connections to the reference DTU can continue while new inbound TCP connections time out. The issue is not currently proven to originate in HEMS code. Continue diagnosis separately using LAN/WAN path isolation and, if needed, lwIP/AsyncTCP PCB/accept instrumentation. Do not mark HEMS milestones complete or incomplete based on an unproven causal link.

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

A future PSRAM image migration remains optional and must not become a HEMS dependency.

## Branch and PR discipline

The previous #18 -> #20 -> #21 stack demonstrated why cross-cutting infrastructure and deep feature stacks are fragile. Use these rules going forward:

1. Keep `integration/hems-next` as the shared base for accepted infrastructure.
2. Prefer one active implementation PR for one coherent milestone.
3. Do not stack a new milestone on an unmerged feature PR unless there is a deliberate, documented reason.
4. Before creating a dependent PR, verify the full branch/PR graph and required shared infrastructure.
5. Before telling an operator to flash a PR build, verify that the target PR actually contains the updater/channel infrastructure, that its `pr-<number>` snapshot is published, and that the correct PIO target exists.
6. Close superseded PRs rather than leaving misleading open dependency chains.

## Documentation rule

This roadmap is a release/continuity artifact, not optional prose.

Update it when any of the following changes:

- current milestone;
- milestone status;
- branch/PR topology;
- integration baseline;
- safety boundary;
- hardware-validation result;
- implementation order;
- deferred/blocking issue that materially affects the plan.

Also update [`HEMS_Current_Status.md`](HEMS_Current_Status.md) when implementation facts change.

Never mark physical validation complete based only on native tests or CI. Electronics are sufficiently creative without the documentation participating.