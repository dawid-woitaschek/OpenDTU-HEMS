# OpenDTU-HEMS Architecture

> Status: architectural baseline for early development.
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
WARP3 Smart           -> HEMS reads telemetry only
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

HEMS has four global operating modes.

### Disabled

The HEMS is inactive. Existing OpenDTU-OnBattery behavior should remain unchanged.

### Monitor

Telemetry is collected and displayed, but no policy decisions are executed.

### Shadow

The full policy and optimizer logic is evaluated, but hardware writes are suppressed.

Example:

```text
Decision: BLOCK_STORAGE_DISCHARGE
Reason: EV_CHARGING
Would write: Marstek -> Block discharge
Execution: SHADOW
```

### Active

Validated HEMS decisions may be written to devices that have explicit external control enabled.

Active mode alone is not sufficient to enable a write. A controllable device also requires a per-device write permission.

```text
HEMS Active
    +
Device external control enabled
    +
Safety checks passed
    =
Hardware write allowed
```

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

Before a write, the safety supervisor should validate at least:

- HEMS is in Active mode;
- device external control is enabled;
- device is reachable;
- required telemetry is fresh;
- no relevant device fault is active;
- SoC and other required values are valid;
- requested mode/target is within configured limits;
- rate limits / minimum runtimes / hysteresis are satisfied;
- the command does not duplicate an unchanged persistent target unnecessarily.

Where the device supports readback, writes should be verified.

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

Initial conceptual order:

```text
1. device safety / faults
2. hard hardware limits
3. manual override
4. EV protection
5. minimum reserve
6. tariff optimizer
7. PV optimization
8. normal operation
```

The UI should show the winning policy and, where practical, suppressed lower-priority decisions.

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
