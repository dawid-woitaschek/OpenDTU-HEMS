# OpenDTU-HEMS development roadmap

Snapshot: 2026-10-22. Product branch: `hems`, currently containing the promoted modernization/provider work through PR #28 and the memory/TLS hardening promoted by PR #30. Current `hems` promotion head: `617ef19a3af6e64b81a349a867c16facaae6aa04`.

Foundation/productization PR #23, policy PR #24, economic dispatch PR #25, modernization/provider PR #27 and memory/TLS hardening PR #29 are merged in the promoted HEMS line. PR #26 was superseded by #27. PR #28 promoted the modernization/provider state to `hems`; PR #30 promoted the memory/TLS hardening state to `hems`.

Open PR #31 (`feature/cmt-radio-rework`) is intentionally **not part of `hems`**. It ports CMT2300A recovery/RX-hopping behavior from current upstream OpenDTU work and remains unmerged and physically unvalidated. Its source base is current `hems` at `617ef19a3af6e64b81a349a867c16facaae6aa04`.

## Product model implemented in `hems`

OpenDTU-HEMS is supervisory home-energy automation, not the primary hardware safety controller. Hardware protections remain authoritative.

- Global Disabled / Monitor / Active, persisted with legacy migration and invalid-value fallback to Monitor.
- Independent Storage and EVSE Monitor only / HEMS control permissions.
- Authenticated manual semantic control in Monitor or Active when target readiness allows it.
- Active automatic commands require target write permission; Monitor emits no automatic writes.
- EV protection observes charging telemetry, blocks new storage discharge and stops only fresh observed storage discharge. Release follows 120 seconds of continuous fresh-false only after an actual intervention.
- Manual and automatic commands share the existing provider worker/client/mailbox and semantic actuator path.
- Economic storage dispatch is implemented as an explicit default-off opt-in. Manual storage commands durably pause economic automation until authenticated Resume.
- Durable recovery/ownership journals and temporary-session authorization are not part of the product model. Old recovery files are ignored and administratively removable.
- Manual UI, command/readback/telemetry diagnostics, runtime capability diagnostics and profile replacement without reboot are implemented.
- Advanced Diagnostics can create a privacy-redacted `opendtu-hems-diagnostics/v1` support snapshot entirely in the browser. No persistent diagnostic history is written to LittleFS.
- Overview presents the generated storage plan as a first-class schedule: current action, next action change, charge/hold/discharge blocks, expected SoC progression, strategy summary and modeled benefit.
- Normal Settings keeps reserve/upper-SoC, grid-charging, economic-discharge and target/deadline controls together; technical optimizer/model knobs stay under Advanced settings.
- Dynamic tariff and optional PV forecast domains, local/JSON provider paths, native Energy-Charts DE-LU v2 pricing and Forecast.Solar support are implemented.
- Repository-backed device-profile distribution with browser-side integrity verification and authenticated import is implemented.
- Runtime memory/TLS hardening and bounded heap diagnostics are implemented. ESP32-S3 no-PSRAM hardware validation over more than 24 hours confirmed successful Forecast.Solar TLS cycles and recovery of the largest INTERNAL free block after TLS activity; no provider memory leak was established.

See [current status](HEMS_Current_Status.md), [control model](HEMS_CONTROL.md), [energy data](HEMS_Energy_Data.md), [native energy/optimizer](HEMS_Native_Energy.md), [dispatch](HEMS_Economic_Dispatch.md) and [policies](HEMS_Policies.md).

## Next milestones

1. **Physical WARP3 + Marstek validation:** on Fusion v2.1 + Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`. Exercise WARP current/stop/release and Marstek acquire/charge/discharge/stop/SoC/release; all four permission combinations; Monitor and Active; communication interruption and return; best-effort permission/configuration release. Record actual physical behavior separately from register readback. Use the browser-only diagnostic snapshot before/after representative runs.
2. **Correct actual WARP/Marstek mapping or lifecycle issues** found on hardware. Verify Marstek V148 power/SoC scaling, sign, persistence and command/watchdog behavior. Do not invent lifecycle architecture without hardware evidence.
3. **Live provider/on-device validation:** Energy-Charts v2 HTTPS/parser/heap/stack and Forecast.Solar DNS/TLS/resources, scheduling/quota behavior and real multi-plane responses. Memory/TLS hardening is hardware-validated broadly, but provider-specific functional validation still needs representative live runs and retained diagnostics.
4. **Economic optimizer/dispatch real-input validation:** confirm planner timing/resources and dispatch behavior on-device with live tariffs, forecast data and fresh battery telemetry. Software behavior is implemented; physical charge/discharge behavior remains the gate.
5. **Product UX / planning presentation:** perform real-device visual validation and polish of Overview, Settings, Devices and Diagnostics. Keep one authoritative control model; tariff categories remain presentation-only.
6. **CMT2300A radio recovery/RX hopping:** PR #31 is open and unmerged. Review CI separately when available, then perform hardware validation before any merge or promotion. Do not claim RF reliability from CI alone. If accepted, update both this roadmap and Current Status in the same PR before merge/promotion.
7. **Deferred economic policy expansion:** minimum-economic-benefit/hysteresis and recurring target/reserve schedules after real dispatch behavior is validated. Avoid a generic rule-engine DSL.
8. **Economic EV charging:** define product semantics first, then add an EVSE optimizer/dispatch policy through the existing semantic actuator and permission model. No automatic economic EVSE producer exists yet.
9. **Optional inverter firmware-update research:** reverse-engineered Hoymiles update transport exists in historical community work, but this is explicitly deferred and is not part of current `hems`. Any future implementation must be isolated, fail-closed on compatibility and physically validated before promotion.
10. **Polish, documentation and release.**

Identity/recovery work remains optional unless physical testing proves a concrete operational need. There is no architectural mega-milestone before useful operation.

## Current open work and validation boundaries

### PR #31 - CMT2300A radio rework

Open against `hems`, source branch `feature/cmt-radio-rework`, current documented head `a9f0592a0fbbfc3adf1324d397272252a64783d0`.

The PR ports current upstream CMT2300A recovery, probing, channel-change acknowledgement handling, per-inverter heard/link state and RX-ring following while retaining HEMS/OnBattery behavior. It intentionally leaves NRF source behavior unchanged except for shared default-compatible abstractions.

The initial six-commit head previously had passing CI as documented in the PR. Later follow-up commits were pushed to align with newer upstream behavior, but their CI status is not asserted here. Hardware validation has not been performed. Therefore PR #31 is **unmerged and physically unvalidated** and must not be described as current `hems` behavior.

## Verification and resource discipline

Focused native tests and frontend checks may be used during development. GitHub CI owns the complete firmware matrix and PR firmware-channel publication. Do not waste development time polling remote CI; evaluate results when they are available and act on failures then.

Before recommending flashing or promotion, verify the exact tested source/merge tree, all HEMS targets including `generic_esp32s3_usb`, manifest SHA256/size and normal OTA image. Physical validation must remain distinct from software/CI validation.

The support snapshot is a support/validation transport, not a logging subsystem: it is generated only on demand, secrets are always redacted, network/location data are opt-in, and no persistent diagnostic history is created on the device.

The no-PSRAM `generic_esp32s3_usb` target remains the acceptance baseline. PR #29/#30 added 960 bytes static DRAM for fixed diagnostics and no PSRAM requirement, periodic restart, TLS weakening or TCP/IP/AsyncTCP stack redesign.

The separate intermittent inbound-new-connection investigation remains unproven as a HEMS issue. It does not block the product model.
