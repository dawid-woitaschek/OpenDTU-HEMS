# OpenDTU-HEMS Architecture

> Status: supervisory control model; updated 2026-10-15.
>
> This document describes the intended design constraints and responsibilities of OpenDTU-HEMS. Individual implementation details may evolve as real hardware integrations are validated, but changes to the core principles should be deliberate and documented.

## 1. Purpose

OpenDTU-HEMS extends OpenDTU-OnBattery with a local-first Home Energy Management System (HEMS).

The project keeps the existing OpenDTU-OnBattery functionality, especially inverter communication, power-meter integration, battery/BMS integration, MQTT, web UI and the Dynamic Power Limiter (DPL), and adds a slower strategic control layer for:

- AC- and DC-coupled energy storage;
- EV charger telemetry and optional control;
- dynamic electricity tariffs;
- PV and load forecasts;
- reserve and state-of-charge policies;
- economic charge/discharge decisions;
- observable, fail-safe energy-management decisions.

The HEMS must remain useful without a mandatory cloud service, Home Assistant instance, Node-RED server or external database.

## Current operational model

OpenDTU-HEMS is supervisory home-energy automation. Hardware/device protections remain authoritative; HEMS command failures are operational failures, not permanent authorization lockouts.

Storage and EVSE each have an independent Monitor only / HEMS control permission consumed by the logic core and provider. Manual semantic commands work in Monitor and Active when target readiness permits them. Automatic writes additionally require Active. Disabled preserves disabled provider behavior. Policies expose both proposals and actionable constraints; a monitor-only EVSE can provide telemetry for writable-storage EV protection.

One worker/client and depth-one mailbox per provider serialize all telemetry, manual commands, automatic commands and best-effort release. Commands are profile-bounded and generation-pinned, with last-moment permission/freshness checks and register readback. A command failure closes the client, records the error and schedules telemetry; fresh communication restores availability automatically.

Recovery/release is best effort and diagnostic. No durable ownership intent, identity/replay eligibility, attestation or temporary session lies in the normal command path. Known old ownership receives a single retained-endpoint release attempt on change; it cannot permanently block a new configuration. Old #22 journal files are ignored and administratively removable.

See [HEMS_CONTROL.md](HEMS_CONTROL.md) for the implemented contract and [HEMS_Roadmap.md](HEMS_Roadmap.md) for the short remaining path to release.

## 2. Core design principles

### Preserve the OpenDTU-OnBattery core

Existing OpenDTU-OnBattery behavior must remain available. HEMS-specific functionality must be opt-in and must not silently change existing device behavior after an update.

### Local first

Local interfaces such as Modbus TCP, local HTTP APIs and MQTT are preferred over vendor cloud APIs whenever possible.

### Separate fast control from slow strategy

The existing DPL remains responsible for fast physical power regulation.

The HEMS optimizer is responsible for slower strategic decisions such as:

- whether a storage system may charge or discharge;
- whether a reserve SoC must be kept;
- whether grid charging is economically useful;
- whether an actively charging EV should block storage discharge;
- whether predicted PV generation should keep storage capacity free.

The HEMS must not turn the DPL into a tariff, forecast and rule engine.

### One actuator, one boss

A controllable device should have one authoritative control path.

For the initial reference installation:

```text
Hoymiles inverter(s)  <- DPL controls output
Marstek storage       <- HEMS may control storage policy
WARP3 Smart           <- HEMS reads telemetry and optionally controls current/stop/release
```

Home Assistant, vendor apps and other automation systems may observe state, but simultaneous autonomous writers should be avoided.

### Read before write

Every new external integration should be implemented read-only first where technically possible.

The intended development order is:

1. connectivity;
2. telemetry;
3. data validation and freshness;
4. shadow decisions;
5. explicit write-layer tests;
6. active automation.

### Observable decisions

The system should expose what it decided and why.

A useful HEMS status should be able to answer:

```text
What is the current decision?
Why was it selected?
Which rule or policy won?
Which inputs were used?
How old are those inputs?
Would a hardware command be sent in Active mode?
```

## 3. Supported hardware baseline

### Minimum target

OpenDTU-HEMS should remain fully functional on supported ESP32-family targets with:

- at least 8 MB flash;
- sufficient free heap for the existing OpenDTU-OnBattery functionality;
- Wi-Fi and/or Ethernet networking.

4 MB targets are not an official HEMS target because firmware size already limits OTA support in OpenDTU-OnBattery.

### PSRAM policy

PSRAM is optional.

The complete core HEMS functionality must work without PSRAM. If PSRAM is available, it may be used for non-critical or larger temporary data structures such as:

- larger tariff/forecast buffers;
- larger import/export profile buffers;
- extended diagnostics;
- longer temporary histories;
- larger JSON parsing buffers.

A missing PSRAM capability must not change the fundamental policy result of the HEMS.

In short:

```text
>= 8 MB flash, no PSRAM
    -> full HEMS core functionality

>= 8 MB flash + PSRAM
    -> same functionality with more memory headroom / richer diagnostics
```

### Reference development hardware

Initial reference devices include:

- OpenDTU Fusion v2, ESP32-S3, 8 MB flash, LAN/W5500 available, PSRAM optional depending on build target;
- generic ESP32-S3 systems with at least 8 MB flash;
- a second ESP32-S3 reference system with 16 MB flash, 8 MB PSRAM and W5500 Ethernet.

The HEMS implementation itself must not depend on Fusion-specific pin mappings.

## 4. Build target strategy

The HEMS CI should cover representative >=8 MB targets rather than one specific board only.

Initial build matrix:

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

Both OTA and factory binaries should be generated for each supported target.

A future firmware-size guard should warn or fail before an image becomes too large for the 8 MB OTA partition layout.

## 5. High-level architecture

```text
                 Tariffs / Forecasts
                         |
                         v
                 +---------------+
                 |  HEMS State   |
                 +-------+-------+
                         |
             +-----------+-----------+
             |                       |
             v                       v
      Constraint / Rule Engine   Diagnostics
             |
             v
       Energy Optimizer
             |
             v
      Safety Supervisor
        |            |
        v            v
    Storage       Constraints
    provider          to DPL
```

External device providers update normalized state. The optimizer consumes snapshots of this state and produces semantic decisions. The safety supervisor validates those decisions before any hardware write is permitted.

## 6. HEMS operating modes

HEMS has three global modes: Disabled, Monitor and Active.

Disabled stops automatic control and HEMS polling. Monitor calculates observable policy proposals and permits authenticated manual semantic commands for target types set to HEMS control. Active additionally permits automatic policy commands for those writable target types. Manual commands in Active may be overridden by automation. Shadow is policy behavior in Monitor, not an additional global mode.

## 7. State store

External providers should not be queried directly from policy code. They update a normalized HEMS state store.

Conceptually:

```text
HEMS State

Grid
  power
  valid
  age

PV
  power
  valid
  age

Storage
  SoC
  power
  mode
  limits
  fault
  valid
  age

EVSE
  vehicle connected
  charging
  power
  session energy
  fault
  valid
  age

Tariff
  current price
  future slots
  validity

Forecast
  expected generation
  validity
```

The optimizer evaluates snapshots, not live blocking network calls.

## 8. Data freshness

Every external value must carry validity and data age.

A stale or missing value must not silently be interpreted as a valid zero/false value.

Bad:

```cpp
if (!charging) {
    allowStorageDischarge();
}
```

Preferred semantic behavior:

```text
charging.valid == true
AND
charging.value == false
```

If the EVSE has not responded for a defined timeout, the state is `unknown`, not `not charging`.

Freshness thresholds are provider-specific and configurable where appropriate.

## 9. EVSE integration

EV chargers are modeled as observable loads with optional control capabilities.

A normalized snapshot may contain:

```cpp
struct EvseSnapshot {
    bool valid;
    bool reachable;
    bool vehicleConnected;
    bool charging;
    int32_t powerW;
    uint64_t sessionEnergyWh;
    uint32_t ageMs;
};
```

Provider capabilities must be explicit:

```text
Telemetry
  yes/no

Control
  current limit
  enable/disable
  phase switching
  none

Failsafe
  watchdog
  finite lease
  persistent
```

### WARP3 reference integration

For the initial WARP3 integration, HEMS uses Modbus TCP telemetry only.

The HEMS must not control the WARP3 in the initial implementation. The charger stays autonomous if the DTU fails.

Typical telemetry includes:

- vehicle connected state;
- charging state;
- current charging power;
- allowed charging current;
- session energy;
- data age / communication status.

A first useful policy is to prevent a home storage system from discharging into an actively charging EV.

## 10. Energy storage integration

An AC-coupled storage system should not be forced into the existing BMS-oriented OpenDTU-OnBattery battery abstraction.

The HEMS therefore uses a separate semantic storage layer.

Conceptual snapshot:

```cpp
struct StorageSnapshot {
    bool valid;
    bool reachable;
    float socPct;
    int32_t powerW;
    int32_t maxChargePowerW;
    int32_t maxDischargePowerW;
    bool fault;
    uint32_t ageMs;
};
```

Power sign convention:

```text
positive storage power = charging
negative storage power = discharging
```

Providers translate manufacturer-specific registers into this common model.

## 11. Semantic storage modes

The HEMS should reason in semantic modes instead of raw Modbus register values.

Initial set:

```cpp
enum class StorageMode {
    Normal,
    BlockCharge,
    BlockDischarge,
    BlockChargeAndDischarge,
    ForceCharge,
    ForceDischarge
};
```

The provider or device profile maps these modes to manufacturer-specific commands.

This idea deliberately follows the useful abstraction used by Tinkerforge's battery-control implementation while keeping the OpenDTU-HEMS data model independent.

## 12. Modbus TCP layer

A shared asynchronous Modbus TCP layer should be used by device providers.

Requirements:

- non-blocking operation;
- usable over Wi-Fi and Ethernet;
- no network timeout inside the fast DPL path;
- configurable host, port, unit ID and polling interval;
- centralized error/timeout counters;
- clean reconnect behavior;
- read/write request separation;
- provider-level data freshness.

The exact third-party library should be selected only after compatibility is verified on the reference ESP32, ESP32-S3 and W5500 targets.

## 13. Modbus device profiles

OpenDTU-HEMS should support reusable Modbus profiles instead of hard-coding all register maps into HEMS policy logic.

Profiles are intended to describe:

- transport defaults;
- telemetry registers;
- value types and scaling;
- semantic storage modes;
- write sequences;
- device limits;
- failsafe behavior;
- metadata and provenance.

Conceptual format:

```json
{
  "schema": "opendtu-hems-modbus-profile/v1",
  "id": "vendor.model.example",
  "metadata": {
    "vendor": "Example",
    "model": "Example Storage",
    "source": "vendor-documentation",
    "source_revision": "..."
  },
  "transport": {
    "protocol": "modbus_tcp",
    "default_port": 502,
    "unit_id": 1,
    "address_base": 0
  },
  "telemetry": {},
  "control": {},
  "failsafe": {
    "type": "persistent"
  }
}
```

Real register values must only be added after verification against reliable documentation and/or real hardware.

## 14. Profile sources and import

Three profile origins are planned.

### Built-in

Profiles shipped and tested with OpenDTU-HEMS.

### Imported

User-supplied OpenDTU-HEMS JSON profiles.

Importing a profile never enables write access automatically.

### Tinkerforge-derived

Tinkerforge maintains manufacturer-specific battery Modbus register definitions in:

`Tinkerforge/esp32-firmware/software/src/modules/batteries_modbus_tcp`

A future build-time conversion tool may import supported definitions into the OpenDTU-HEMS profile format.

The ESP32 must not download and execute upstream Python source at runtime.

Converted definitions should record:

- source repository;
- source commit/revision;
- generation date;
- profile schema version.

Licensing and attribution requirements must be reviewed before distributing converted definitions.

## 15. Write safety

A profile that contains write registers is not enough to authorize writes.

Write enablement requires explicit user action.

Normal semantic writes require target HEMS-control permission, configured device/profile, reachability, fresh telemetry, supported bounded operation and serialized worker access. Automatic writes additionally require Active. Manual writes work in Monitor. Readback is verified where supported. Physical protections stay authoritative.

Release/cleanup is best effort, never a durable authorization prerequisite. Optional identity/recovery/lifecycle metadata may inform diagnostics and hardware validation, but cannot block normal commands. Old journal files are ignored and can be cleared administratively.

## 16. Failsafe model

Control providers should expose how a command behaves if communication stops.

Examples:

```text
Watchdog
  command must be refreshed or device falls back automatically

Finite lease
  command has an explicit lifetime

Persistent
  command remains active until another command changes it
```

Persistent commands require stricter safety handling than watchdog-based commands.

## 17. First reference storage

A Marstek Venus E 3.0 is the first planned AC-storage reference device.

Development order:

1. verify Modbus register map;
2. read-only connectivity;
3. telemetry mapping;
4. long-running read-only validation;
5. shadow policy decisions;
6. explicit manual write tests;
7. automated active control.

No write register should be invented or inferred from unrelated models.

## 18. First reference policy

The first useful policy is deliberately simple.

```text
IF
    WARP telemetry is valid
AND WARP is actively charging
AND charging power > configured threshold
THEN
    desired storage mode = BlockDischarge
```

After charging stops, a configurable debounce period should keep the policy active briefly to avoid oscillation during short charging interruptions.

Initial reference debounce: approximately 120 seconds.

This policy is implemented and validated in Shadow mode before Active mode exists.

## 19. Policy priority

Multiple rules may request conflicting behavior. The system therefore needs deterministic priorities.

Current deterministic order (the earlier manual-before-protection concept is superseded by the implemented control semantics):

1. Device/BMS safety and hard hardware limits.
2. EV Protection and authoritative current control constraints.
3. Explicit authorized manual semantic actions.
4. Economic user policies → optimizer → Shadow plan (zero economic writes).

Active EV Protection rejects protected discharge even when manually requested.
Manual and automatic actions share one permission-gated semantic actuator path.
The optimizer applies current protection constraints to the first slot; future EV
state remains unknown and would have to be checked again at eventual dispatch.
No economic producer can override manual commands in this milestone. An explicit
manual pause/resume model is required before later Active economic dispatch.

Policy configuration defines what is allowed/desired; pure policy evaluation
produces constraints and deadline goals. The existing DP decides when/how to meet
them using exact prices, usable PV, SoC, powers and losses. Tariff classification
is presentation-only. See [precise policy contract](HEMS_Policies.md).

## 20. Tariff model

Dynamic tariff data should be normalized into time slots, preferably 15-minute intervals where available.

A tariff provider may expose:

- raw market price;
- effective import price;
- taxes and variable grid components where relevant;
- validity interval;
- future slots.

The optimizer should use the actual end-customer energy cost whenever available rather than assuming the wholesale price is the import price.

## 21. Economic optimizer

The optimizer must account for storage efficiency.

At minimum:

```text
effective stored-energy cost = current import price / round-trip efficiency

economic advantage = future avoided import price
                   - effective stored-energy cost
                   - optional cycle cost
```

Later iterations should consider:

- current SoC;
- usable capacity;
- minimum/maximum SoC;
- maximum charge/discharge power;
- multiple cheap and expensive time slots;
- PV forecasts;
- optional cycle/degradation cost;
- feed-in tariffs.

The optimizer is first implemented in Shadow mode.

## 22. Forecast providers

Forecast inputs are optional strategy inputs.

Potential providers include:

- generic HTTP/JSON endpoints;
- Forecast.Solar;
- Open-Meteo based calculations;
- MQTT / Home Assistant supplied values.

A forecast failure must not stop the local physical power regulation.

## 23. Scheduling and timing

No slow network operation may block the DPL control path.

Conceptual cadences:

```text
DPL                     existing fast loop
HEMS state evaluation   ~1-2 s
EVSE polling            ~2-5 s
Storage polling         ~2-5 s
Policy evaluation       event-driven / ~2 s
Tariff planning         on update / ~60 s
Forecast refresh        ~15-60 min
```

Exact intervals are provider-specific and may change during testing.

## 24. Web UI

OpenDTU-HEMS should integrate with the existing OpenDTU-OnBattery web application instead of introducing a separate management service.

Planned routes:

```text
/info/hems
/settings/hems
```

### HEMS Info

Should show, over time:

- operating mode;
- current decision;
- decision reason;
- data freshness;
- grid power;
- storage state;
- EVSE state;
- tariff state;
- forecast state;
- shadow/active execution status.

### HEMS Settings

Should grow with implemented features and may contain:

- General;
- Devices;
- Storage;
- EVSE;
- Tariff;
- Strategy;
- Modbus Profiles.

UI controls must not be exposed before the corresponding backend functionality exists.

## 25. Diagnostics

HEMS diagnostics should eventually expose:

- free heap and PSRAM;
- provider poll success/error counts;
- last successful poll;
- reconnect counts;
- state age;
- policy decision count;
- hardware write count;
- active profile and profile hash;
- firmware build SHA;
- HEMS loop latency.

High-frequency persistent flash logging should be avoided. A bounded in-memory event history is preferred.

## 26. MQTT and Home Assistant

MQTT and Home Assistant are optional observers/integration points, not required control dependencies.

Potential telemetry topics include:

```text
hems/mode
hems/decision
hems/decision_reason
hems/storage/soc
hems/storage/power
hems/storage/mode
hems/evse/charging
hems/evse/power
hems/tariff/current
```

The core HEMS must continue to function if MQTT or Home Assistant is unavailable.

## 27. Configuration compatibility

Existing OpenDTU-OnBattery configurations must remain usable.

If no HEMS configuration exists, HEMS defaults to disabled.

A versioned HEMS configuration block should be used, conceptually:

```json
{
  "hems": {
    "version": 1,
    "enabled": false
  }
}
```

Schema migrations should be explicit when later versions are introduced.

## 28. Secrets

Tariff or forecast providers may require API tokens.

Secrets should:

- not be written to logs;
- be masked in the web UI;
- not be returned unnecessarily by APIs;
- be handled explicitly during configuration export/import.

## 29. Development and test strategy

Each functional change is developed in a small feature branch and merged into `hems` through a pull request.

The intended validation chain is:

```text
Feature branch
    -> CI
    -> review / correction
    -> reference Fusion hardware test when relevant
    -> merge into hems
    -> post-merge smoke test
    -> next milestone
```

A later PR should not be started merely to get ahead while the current milestone is still failing or unvalidated.

## 30. Branch model

```text
development
    upstream sync point

hems
    OpenDTU-HEMS main branch

feature/*, fix/*, ci/*
    isolated work branches merged into hems
```

HEMS-specific changes do not belong in `development`.

Pull requests for OpenDTU-HEMS development must target this repository's `hems` branch, not the upstream `hoylabs/OpenDTU-OnBattery` repository.

## 31. Upstream synchronization

Upstream changes should be synchronized deliberately.

A preferred workflow is:

```text
hoylabs/OpenDTU-OnBattery:development
    -> local/fork development
    -> dedicated sync branch / PR
    -> CI and conflict review
    -> hems
```

This keeps upstream merges observable and testable.

## 32. Non-goals

OpenDTU-HEMS is not intended to become:

- a long-term time-series database;
- a replacement for Grafana;
- a large SQL server;
- a machine-learning training platform;
- a mandatory cloud service;
- a Home Assistant replacement.

Long-term historical analysis may be delegated to external systems while control remains local.

## 33. Definition of done for HEMS PRs

A HEMS PR is not complete merely because it compiles.

Depending on scope, completion should include:

- CI green on the supported build matrix;
- web application builds;
- no unrelated changes outside scope;
- backward-compatible configuration behavior;
- understandable diagnostics/logging;
- explicit rollback path;
- hardware smoke test for relevant changes;
- Shadow validation before automated writes;
- communication-loss and stale-data behavior verified for active control features.

The current milestone and detailed acceptance criteria are tracked in [`HEMS_Roadmap.md`](HEMS_Roadmap.md).

## Historical implementation notes (superseded by the current operational model)

### PR #2 implementation boundary

The first runtime skeleton implements only `Disabled` and `Monitor`. It has no
providers, telemetry snapshots, policy decisions or hardware writes. Monitor
therefore reports `Decision: None` and `External devices: None configured`.

`HemsController` applies configuration at startup, before the Web API starts,
and when HEMS settings change. Its mode is atomic because HTTP handlers and the
main loop run on different tasks. There is no scheduled HEMS task yet: without
telemetry there is no periodic work, including while disabled. Future read-only
providers should use the existing cooperative scheduler and non-blocking I/O.

The native configuration system reads/writes `hems.version` and `hems.enabled`
in `/config.json`. An absent block loads version 1 with HEMS disabled; no change
to the inherited configuration version or migrations is necessary. Unsupported
HEMS versions remain effectively disabled and cannot be edited through the
HEMS API. Explicit migrations must be added when a new schema is introduced.
The existing configuration backup/restore path includes this block. Downgrading
to firmware without HEMS support may discard it on the next configuration save;
back up configuration before downgrading.

The UI uses `/api/hems/config` (authenticated GET/POST) and `/api/hems/status`
(GET with the existing read-only access policy). Settings only toggle enabled;
the effective modes are Disabled and Monitor.

Firmware commit/tag links and the opt-in browser-side GitHub comparison use
`dawid-woitaschek/OpenDTU-HEMS`. The old comparison queried the upstream repository,
where HEMS commits and the `hems` branch do not exist. This is a branch comparison,
not an automatic release installer. GitHub rate limits, network failures and
unpublished local commits can still make version information unavailable. A new
release/update architecture is outside this milestone.

### PR #3 implementation boundary

The controller now dispatches a WARP-specific read-only provider through the
existing scheduler. A small shared Modbus TCP client uses bounded lwIP socket
operations in one isolated worker, outside the scheduler and snapshot mutex.
It has no write interface and shares the native Wi-Fi/W5500 networking stack.
WARP telemetry does not feed DPL or any policy. Optional meter features are
queried before measurements; failures/staleness become null API values.
The normalized store, generic profiles and device control remain future work.

The version-1 configuration gains an optional disabled-by-default WARP block.
This additive change preserves PR #2 configurations without migration.
See [HEMS_WARP.md](HEMS_WARP.md) for provenance, timing/state semantics, resource
bounds, tests and the required physical validation gate.

### PR #4 implementation boundary

The shipped v1 profile schema is deliberately narrower than the conceptual
future profile example above. It contains telemetry, transport defaults, metadata
and provenance only. Unknown fields, including `control` and `failsafe`, are
rejected. Semantic commands, write sequences and runtime failsafe decisions remain
PR #8 work and require a deliberate schema evolution. Import cannot enable writes.

Profiles are loaded on demand from four fixed LittleFS slots, separately from
configuration. No provider, polling task, device activation or DPL input is added.
The shared transport gains only bounded FC3 holding-register reads; WARP still
uses precisely FC2/FC4. Diagnostics decode explicit sample bytes without networking.
See [HEMS_Modbus_Profiles.md](HEMS_Modbus_Profiles.md) for the normative v1 rules,
resource bounds, current Tinkerforge research and remaining hardware validation.


## Current foundation/productization boundary (PR #23)

The earlier numbered implementation-boundary sections are historical. The current
implementation includes normalized telemetry/constraints, profile-defined semantic
control, independent Storage/EVSE permissions, Disabled/Monitor/Active modes and
Active EV Protection. Manual and automatic control share the bounded permission-
gated mailbox/worker/planner/executor path; no competing raw-register API exists.

Native tariff/PV providers and the economic DP produce Shadow plans with **zero
economic device writes**, even in Active. Tariff categories are UI presentation
only. The product UI has Overview, Settings, Devices and Advanced Diagnostics.
See [current status](HEMS_Current_Status.md), [control](HEMS_CONTROL.md),
[native energy/optimizer](HEMS_Native_Energy.md) and [UI](HEMS_UI.md) for current
semantics and remaining physical validation. Practical user/economic policies feed the DP in the next Shadow milestone
(see [policy contract](HEMS_Policies.md)); eventual economic dispatch must use the existing path.
