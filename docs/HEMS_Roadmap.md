# OpenDTU-HEMS development roadmap

Snapshot: 2026-10-23. Product branch: `hems`, currently containing the promoted modernization/provider work through PR #28 and the memory/TLS hardening promoted by PR #30. Current `hems` source checked for the energy-management integration is `693eafe60fb2538ec11019dc0875737eb7e0495c`; the promoted product baseline before the documentation refresh was `617ef19a3af6e64b81a349a867c16facaae6aa04`.

Foundation/productization PR #23, policy PR #24, economic dispatch PR #25, modernization/provider PR #27 and memory/TLS hardening PR #29 are merged in the promoted HEMS line. PR #26 was superseded by #27. PR #28 promoted the modernization/provider state to `hems`; PR #30 promoted the memory/TLS hardening state to `hems`.

Open PR #31 (`feature/cmt-radio-rework`) is intentionally **not part of `hems`**. It ports CMT2300A recovery/RX-hopping behavior from current upstream OpenDTU work and remains unmerged and physically unvalidated. Its source base is the pre-documentation-refresh `hems` baseline `617ef19a3af6e64b81a349a867c16facaae6aa04`.

Draft PR #34 (`codex/hems-energy-management` -> `hems-energy-management`) is a separate **unmerged integration line** based on `hems` SHA `693eafe60fb2538ec11019dc0875737eb7e0495c`. It adds coordinated DPL/storage energy-flow control, optional bounded household-load learning, explicit export policies, marginal battery-wear economics, bounded terminal inventory valuation and monetary plan-change hysteresis while preserving the existing bounded DP. Its software tests and representative `generic_esp32s3_usb` 8 MB/no-PSRAM build passed locally as documented in the PR; it has **not** been physically validated and is not current `hems` behavior.

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

See [current status](HEMS_Current_Status.md), [control model](HEMS_CONTROL.md), [energy data](HEMS_Energy_Data.md), [native energy/optimizer](HEMS_Native_Energy.md), [dispatch](HEMS_Economic_Dispatch.md), [energy management](HEMS_Energy_Management.md) and [policies](HEMS_Policies.md).

## Next milestones

1. **Review and physically validate energy-management integration PR #34:** verify the coordinated DPL/storage control contract on Fusion v2.1 + Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`, without PSRAM. Validate actual Marstek V148 sign/scale, acquire/charge/discharge/Stop/Release semantics, command persistence/watchdog and response latency; verify DPL zero/lower-output behavior, residual-only night operation, dawn/dusk PV control transitions, telemetry timestamp alignment and actual export/import transients. Keep software/build validation distinct from device validation.
2. **Physical WARP3 + Marstek validation of the promoted control model:** exercise WARP current/stop/release and Marstek acquire/charge/discharge/stop/SoC/release; all four permission combinations; Monitor and Active; communication interruption and return; best-effort permission/configuration release. Record actual physical behavior separately from register readback. Use the browser-only diagnostic snapshot before/after representative runs.
3. **Correct actual WARP/Marstek mapping, lifecycle or coordination issues** found on hardware. Do not invent lifecycle or timing guarantees without hardware evidence. PR #34 must remain conservative when coordinated enforcement is unavailable.
4. **Live provider/on-device validation:** Energy-Charts v2 HTTPS/parser/heap/stack and Forecast.Solar DNS/TLS/resources, scheduling/quota behavior and real multi-plane responses. Memory/TLS hardening is hardware-validated broadly, but provider-specific functional validation still needs representative live runs and retained diagnostics.
5. **Economic optimizer/dispatch real-input validation:** compare planned versus commanded versus measured behavior using live tariffs, forecast data and fresh battery telemetry. For PR #34 additionally validate learned-load fallback/coverage, degradation accounting, terminal valuation across repeated replanning and monetary hysteresis against fresh comparable alternatives. Physical charge/discharge behavior remains the gate.
6. **Resource and responsiveness validation for PR #34:** the local no-PSRAM build passed and the PR reports +2,288 B static RAM and +7,008 B learning-enabled peak planning allocations versus its base. Measure runtime heap/largest free block, stack watermarks, planning latency, DPL responsiveness, TLS headroom and actuator/network transients on the actual no-PSRAM ESP32-S3 before promotion.
7. **Product UX / planning presentation:** perform real-device visual validation and polish of Overview, Settings, Devices and Diagnostics. Keep one authoritative control model; tariff categories remain presentation-only.
8. **CMT2300A radio recovery/RX hopping:** PR #31 is open and unmerged. Review CI separately when available, then perform hardware validation before any merge or promotion. Do not claim RF reliability from CI alone. If accepted, update both this roadmap and Current Status in the same PR before merge/promotion.
9. **Post-validation economic refinement:** PR #34 already implements marginal degradation, an optional conservative throughput margin, bounded terminal valuation and monetary hysteresis on its integration line. Further risk calibration, power-dependent efficiency, auxiliary consumption, richer aging models or recurring target/reserve schedules remain deferred until real dispatch and forecast behavior provide evidence. Avoid a generic rule-engine DSL.
10. **Economic EV charging:** define product semantics first, then add an EVSE optimizer/dispatch policy through the existing semantic actuator and permission model. No automatic economic EVSE producer exists yet.
11. **Optional inverter firmware-update research:** reverse-engineered Hoymiles update transport exists in historical community work, but this is explicitly deferred and is not part of current `hems`. Any future implementation must be isolated, fail-closed on compatibility and physically validated before promotion.
12. **Polish, documentation and release.**

Identity/recovery work remains optional unless physical testing proves a concrete operational need. The integration work in PR #34 must not be treated as promoted product behavior until review, hardware validation and a deliberate promotion to `hems`.

## Current open work and validation boundaries

### PR #34 - coordinated energy management integration

Draft PR #34 targets the separate `hems-energy-management` integration branch from `codex/hems-energy-management`. Its source baseline is `hems` SHA `693eafe60fb2538ec11019dc0875737eb7e0495c`; it is not based on or merged into current `hems` product behavior.

The integration preserves the bounded numeric-price DP and adds explicit system energy-flow permissions and net grid-export policy, coordinated DPL/storage execution with a single Hoymiles writer, optional fixed-size local household-load learning with constant fallback, EFC-reference marginal degradation cost, an optional conservative throughput margin, bounded terminal inventory valuation and monetary plan-change hysteresis. New coordination and automatic learning require explicit opt-in; battery export remains separately authorized from export compensation.

Local validation documented by the PR includes 35 native test executables, frontend/browser checks and a representative `generic_esp32s3_usb` 8 MB/no-PSRAM firmware build. The reported resource delta is +2,288 B static RAM and +7,008 B learning-enabled peak planning allocations over the PR base. These are software/build measurements, not live ESP32 performance.

Still unresolved before promotion: physical Marstek V148 behavior and timing, residual-only night behavior and dawn/dusk transitions, grid/PV/storage timestamp alignment, actual export transients and lower-output behavior, actuator failure behavior on real devices, runtime heap/stack/planning latency/DPL responsiveness/TLS headroom, and end-to-end validation of the coordinated topology. Therefore PR #34 is **unmerged and physically unvalidated** and must not be described as current `hems` behavior.

### PR #34 consistency review and retained limitations

The [ten-point review](HEMS_PR34_Review.md) records confirmed code paths, regression invariants and rejected unsafe remedies. The integration now distinguishes residual-only nighttime battery regulation from unavailable daytime DPL control; hands EV Protection ownership to coordinated control without an autonomous Release; cleans up Flow ownership on explicit withdrawal; preserves the effective DPL device/model/global ceiling in execution and planning and supports bounded recovery from acknowledged zero output; retains disabled-mode legacy export semantics; applies LocalFirst charging before curtailment; and suppresses comparisons against infeasible hold baselines. Unknown wear permits shadow planning but gates new active coordinated economics until an estimate or explicit zero acknowledgement is configured.

Still intentionally limited: PV-only charging cannot safely bootstrap from fully curtailed PV when neither grid charging nor PV export is permitted. Planning and reporting expose that restriction; a future explicit bounded probe needs permission and physical response evidence. The bounded avoided-import terminal table does not value empty headroom under negative continuation prices and now reports that limitation. Do not remove the clamp without a credible charging-tail model and repeated-replanning evidence.

These changes add no task, history dimension, synchronous DPL networking or second Hoymiles writer. Static object budgets and `O(T x S^2)` planning remain bounded. PR #34 remains draft, **unmerged and physically unvalidated**, targeting only `hems-energy-management`; it does not change current promoted `hems` behavior.

### PR #31 - CMT2300A radio rework

Open against `hems`, source branch `feature/cmt-radio-rework`, current documented head `a9f0592a0fbbfc3adf1324d397272252a64783d0`.

The PR ports current upstream CMT2300A recovery, probing, channel-change acknowledgement handling, per-inverter heard/link state and RX-ring following while retaining HEMS/OnBattery behavior. It intentionally leaves NRF source behavior unchanged except for shared default-compatible abstractions.

The initial six-commit head previously had passing CI as documented in the PR. Later follow-up commits were pushed to align with newer upstream behavior, but their CI status is not asserted here. Hardware validation has not been performed. Therefore PR #31 is **unmerged and physically unvalidated** and must not be described as current `hems` behavior.

## Verification and resource discipline

Focused native tests and frontend checks may be used during development. GitHub CI owns the complete firmware matrix and PR firmware-channel publication. Do not waste development time polling remote CI; evaluate results when they are available and act on failures then.

Before recommending flashing or promotion, verify the exact tested source/merge tree, all HEMS targets including `generic_esp32s3_usb`, manifest SHA256/size and normal OTA image. Physical validation must remain distinct from software/CI validation.

The support snapshot is a support/validation transport, not a logging subsystem: it is generated only on demand, secrets are always redacted, network/location data are opt-in, and no persistent diagnostic history is created on the device.

The no-PSRAM `generic_esp32s3_usb` target remains the acceptance baseline. PR #29/#30 added 960 bytes static DRAM for fixed diagnostics and no PSRAM requirement, periodic restart, TLS weakening or TCP/IP/AsyncTCP stack redesign. PR #34's reported resource measurements apply only to its unmerged integration branch until physically validated and promoted.

The separate intermittent inbound-new-connection investigation remains unproven as a HEMS issue. It does not block the product model.
