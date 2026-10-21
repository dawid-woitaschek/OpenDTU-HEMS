# OpenDTU-HEMS development roadmap

Snapshot: 2026-10-21. Development baseline: `hems` at
`617ef19a3af6e64b81a349a867c16facaae6aa04` (merged PR #30).
See [current status](HEMS_Current_Status.md) for merged history and validation evidence.

## Implemented software

The HEMS architecture, runtime device-profile framework, normalized telemetry,
semantic control path, independent Storage/EVSE permissions, safety/readiness gates,
EV Protection, economic optimizer, storage policies and Economic Storage Dispatch
are substantially implemented. The next milestone is physical validation of that
software, followed by corrections supported by device evidence.

- Disabled / Monitor / Active and independent Monitor only / HEMS control permissions
  are persisted. Monitor allows authorized manual commands but emits no automatic writes.
- Manual and automatic commands share bounded profile plans, one mailbox/worker/client
  per provider, generation checks, readback and the semantic executor. Hardware/BMS
  protections remain authoritative. Release is best effort and does not guarantee idle.
- Active EV Protection observes even monitor-only EVSE telemetry, blocks new storage
  discharge and stops only fresh observed discharge. Its 120-second continuously
  fresh-not-charging path releases only an actual, unsuperseded protection intervention.
- Exact numeric tariffs, optional PV, efficiencies, reserve/upper SoC, grid-charging
  and economic-discharge permissions, and a one-time target/deadline constrain the DP.
  Calculation itself performs no device writes; tariff categories are presentation-only.
- Economic Storage Dispatch is merged (#25, promoted via #28). Runtime control needs
  explicit default-off opt-in, Active, Storage HEMS control, a valid current interval
  and every live gate. Durable manual pause/explicit Resume, source ownership,
  bounded replanning/retries and semantic cleanup are implemented.
- Modernization/provider work is merged (#27, promoted via #28): verified repository
  profile import, authoritative firmware lookup, Energy-Charts v2 DE-LU pricing,
  Forecast.Solar contract/plane diagnostics and compatible toolchain updates.
- Runtime memory/TLS hardening and bounded heap diagnostics are merged (#29,
  promoted via #30). PR #30 records more than 24 hours of ESP32-S3 operation without
  PSRAM, successful Forecast.Solar cycles on both planes and largest-block recovery.
  This evidence does not validate WARP3, Marstek or live Energy-Charts.
- Overview presents current/next storage actions, schedule blocks, expected SoC and
  modeled benefit. Settings, Devices and Advanced Diagnostics expose configuration,
  manual controls and detailed results. On-demand browser support snapshots include
  the bounded heap history, redact secrets and default to hiding network/location data.

Contracts: [control](HEMS_CONTROL.md), [profiles](HEMS_Modbus_Profiles.md),
[energy data](HEMS_Energy_Data.md), [native providers/optimizer](HEMS_Native_Energy.md),
[storage policies](HEMS_Policies.md), [dispatch](HEMS_Economic_Dispatch.md).

## Next milestones

1. **WARP3 and Marstek physical validation.** Follow the
   [hardware validation runbook](HEMS_Hardware_Validation.md): fresh telemetry,
   WARP SetCurrent/Stop/Release and charging detection; Marstek Acquire/Charge/
   Discharge/Stop/Target SoC/Release, actual sign/scale, persistence/watchdog;
   all permission combinations, configuration changes, loss and return of connectivity.
   Record transport/readback separately from physical behavior. Correct mappings or
   lifecycle behavior only when hardware evidence establishes the issue.
2. **EV Protection end to end.** Validate real WARP3 charging against real Marstek
   discharge, charging and idle; monitor-only EVSE observation; the continuous
   120-second fresh-false release path, stale/unknown interruption and supersession.
3. **Economic Storage Dispatch end to end.** After the device gates pass, validate
   real charge/hold/discharge from the exact current optimizer interval, default-off
   and permission gates, manual pause/Resume/reboot, protection priority, authority
   loss, bounded retry and ownership-specific cleanup. Measure planner/dispatcher
   timing, heap and worker stack on the reference no-PSRAM ESP32-S3.
4. **Remaining native-provider validation.** Energy-Charts v2 still needs successful
   live ESP32 HTTPS/parser/resource validation. Forecast.Solar has successful hardware
   cycles after #29/#30; broader installations, plane counts, quota/recovery and
   sustained runtime remain regression checks. External HTTP captures and fixtures
   establish provider contracts, not ESP32 execution.
5. **Product UX and release preparation.** Validate the existing planning/control
   presentation on real devices, preserve diagnostics and review unresolved hardware
   findings. Identity/recovery journals are not a prerequisite unless evidence shows
   a concrete operational need.
6. **Deferred policy expansion.** Minimum-economic-benefit/hysteresis and recurring
   target/reserve schedules follow real dispatch validation. Economic EV Charging is
   intentionally **not implemented**: define its product semantics before adding an
   EVSE optimizer/dispatch producer through the existing actuator and permissions.

## Separate open work

As of this snapshot, [PR #31](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/31)
(`feature/cmt-radio-rework`, OpenDTU #3296 port) is **open and unmerged**, still needs
radio hardware validation and is not part of this `hems` baseline. HEMS validation
readiness has no dependency on that PR, its APIs or radio behavior. This work remains
applicable whether #31 is later merged or closed.

## Verification discipline

Use focused checks for changed software; GitHub CI owns the full firmware matrix
and firmware publication. Do not run the full CI locally or wait for, poll or monitor
GitHub Actions for this validation-readiness work. Before a later hardware run,
record the selected firmware's source/tested-image commits, target, size and SHA256;
use a matching normal OTA image, not a factory image. Historic PR image sizes and
channels are not instructions for the current baseline.

Reference hardware: Fusion Board v2.1 + Shield v1.1, ESP32-S3, 8 MB flash,
`generic_esp32s3_usb`, without PSRAM. Builds, host tests and register readback do not
prove physical charging, discharging, watchdog behavior or network timing. The
browser support snapshot is an on-demand evidence transport, not persistent device
logging; its sequential API observations are not an atomic timeline. Keep hardware
outcomes and software validation results separate.
