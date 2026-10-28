# OpenDTU-HEMS development roadmap

Snapshot: 2026-10-28. Product branch: `hems`, containing the promoted modernization/provider work through PR #28, memory/TLS hardening promoted by PR #30, **merged PR #35 runtime/PSRAM hardening, refreshed PR #31 CMT2300A rework, PR #36 cleanup and PR #34 coordinated energy management**.

Foundation/productization PR #23, policy PR #24, economic dispatch PR #25, modernization/provider PR #27 and memory/TLS hardening PR #29 are merged in the promoted HEMS line. PR #26 was superseded by #27. PR #28 and PR #30 promoted those earlier milestones.

PR #35 merged at `f54da01e5fb5086fe5fdf448f7c4dd2e11ecdc3c`; PR #31 was rebased onto that hardened `hems` and refreshed to upstream OpenDTU PR #3296 head `85841577d4b55493732519edf021895b3353e759`. Its new CMT behavior has no additional physical validation. PR #36 cleanup is preserved. PR #34 was rebased onto that combined `hems` head (`49a2526b`) and merged; physical energy-management validation remains pending.

## Product model implemented in `hems`

OpenDTU-HEMS is supervisory home-energy automation, not the primary hardware safety controller. Hardware protections remain authoritative.

- Global Disabled / Monitor / Active, persisted with legacy migration and invalid-value fallback to Monitor.
- Independent Storage and EVSE Monitor only / HEMS control permissions.
- Authenticated manual semantic control in Monitor or Active when target readiness allows it.
- Active automatic commands require target write permission; Monitor emits no automatic writes.
- EV protection (default on, configurable under coordination) observes charging telemetry, blocks new storage discharge and stops only fresh observed storage discharge. Legacy release follows 120 seconds of continuous fresh-false only after an actual intervention; coordinated control instead hands Stop custody to Flow without restoring autonomous battery export.
- Manual and automatic commands share the existing provider worker/client/mailbox and semantic actuator path.
- Economic storage dispatch is implemented as an explicit default-off opt-in. Manual storage commands durably pause economic automation until authenticated Resume.
- Coordinated DPL/storage energy flows with independent export permissions and grid limits; optional bounded household-load learning with constant fallback. Coordination and learning default off.
- Marginal degradation costs, optional throughput margin, bounded terminal inventory valuation and fresh-alternative monetary hysteresis within the existing bounded global DP.
- Durable recovery/ownership journals and temporary-session authorization are not part of the product model. Old recovery files are ignored and administratively removable.
- Manual UI, command/readback/telemetry diagnostics, runtime capability diagnostics and profile replacement without reboot are implemented.
- Advanced Diagnostics can create a privacy-redacted `opendtu-hems-diagnostics/v1` support snapshot entirely in the browser. The on-demand system request includes the existing bounded INTERNAL/PSRAM heap journal and allocation-failure records (`heap_history=1`); ordinary polling is unchanged. No persistent diagnostic history is written to LittleFS.
- Overview presents the generated storage plan as a first-class schedule: current action, next action change, charge/hold/discharge blocks, expected SoC progression, strategy summary and modeled benefit.
- Normal Settings keeps reserve/upper-SoC, grid-charging, economic-discharge and target/deadline controls together; technical optimizer/model knobs stay under Advanced settings.
- Dynamic tariff and optional PV forecast domains, local/JSON provider paths, native Energy-Charts DE-LU v2 pricing and Forecast.Solar support are implemented.
- Repository-backed device-profile distribution with browser-side integrity verification and authenticated import is implemented.
- Runtime memory/TLS hardening and bounded heap diagnostics are implemented. A historical ESP32-S3 no-PSRAM run over more than 24 hours showed successful Forecast.Solar TLS cycles and recovery of the largest INTERNAL block; no provider leak was established. Later Device A pressure/panics supersede no-PSRAM acceptance (see below).

See [current status](HEMS_Current_Status.md), [control model](HEMS_CONTROL.md), [energy data](HEMS_Energy_Data.md), [native energy/optimizer](HEMS_Native_Energy.md), [dispatch](HEMS_Economic_Dispatch.md), [energy management](HEMS_Energy_Management.md) and [policies](HEMS_Policies.md).

## Next milestones

1. **Physical WARP3 + Marstek validation:** on Fusion v2.1 + Shield v1.1, ESP32-S3, 8 MB flash, 2 MB quad PSRAM, `generic_esp32s3_usb_quad_psram`. Exercise WARP current/stop/release and Marstek acquire/charge/discharge/stop/SoC/release; all four permission combinations; Monitor and Active; communication interruption and return; best-effort permission/configuration release. Record actual physical behavior separately from register readback. Use the [hardware validation runbook](HEMS_Hardware_Validation.md) and browser-only diagnostic snapshots before/after representative runs.
2. **Correct actual WARP/Marstek mapping or lifecycle issues** found on hardware. Verify Marstek V148 power/SoC scaling, sign, persistence and command/watchdog behavior. Do not invent lifecycle architecture without hardware evidence.
3. **Live provider/on-device validation:** Energy-Charts v2 HTTPS/parser/heap/stack and Forecast.Solar DNS/TLS/resources, scheduling/quota behavior and real multi-plane responses. Prior successful no-PSRAM runs do not establish long-term stability; the accepted PR #35 PSRAM run covers approximately 21 hours on Device A, with representative provider-specific live runs and retained diagnostics still useful for broader coverage.
4. **Economic optimizer/dispatch real-input validation:** confirm planner timing/resources and dispatch behavior on-device with live tariffs, forecast data and fresh battery telemetry. Merged PR #34 adds learned-load coverage/fallback, wear, terminal valuation and monetary hysteresis. Validate planned versus commanded versus measured flows, zero/changing export limits, stale telemetry, device failures and strict PV-only startup. Measure INTERNAL/PSRAM free/largest blocks, stack watermarks, planning latency, TLS headroom and DPL responsiveness on the combined PSRAM build. Physical charge/discharge and export transients remain gates.
5. **Product UX / unified WebUI (`redesign-v1`, unmerged draft PR #37):** the first
   redesign and a focused UX/IA cleanup are implemented separately from `hems`,
   based on `6b45ebcd`. One concise Overview replaces the classic/HEMS split;
   price, forecast and battery-plan details expand without repeated current facts.
   The sidebar owns navigation and nested active states. Plant distinguishes
   Solarladeregler / Solar Charger from total Solar and offers a human-readable
   inverter summary separate from detailed Live Data. DPL configuration belongs
   to Energiemanagement. Header connection state comes from one app-owned live
   socket; device health and current/last-known values remain distinct. Unverified
   inverter overview measurements and limits show `—`, including cached zero
   values. Help stays attached to labels/headings and supports touch, outside
   dismissal and keyboard use. German and English are the complete supported locale set;
   obsolete non-DE/EN pack assets are removed and language-pack documentation
   describes only the supported set and existing EN/DE display overrides.
   Existing APIs, permissions, forms and opt-in behavior remain authoritative;
   firmware changes only remove unsupported presentation locales and provide
   English fallbacks for old display settings. Existing alarm identifiers and
   German/English messages are preserved. The header uses the supplied official
   OpenDTU-HEMS SVG beside the existing wordmark, replacing the house icon;
   the live-connection indicator remains to the right.
   The SVG is bundled into embedded JavaScript rather than relying on an
   unserved `/assets/` URL; header regression checks enforce this constraint.
   Focused frontend/type/build/Node checks and all four intercepted-API browser
   suites passed locally; the route audit covers EN/DE at 320/768/1440 px,
   including attached help and mobile overflow fixes. [Current status](HEMS_Current_Status.md#product-ui)
   records their scope and remaining live-device visual/physical validation.
   The final telemetry/pack cleanup passed focused type/build/lint/Node checks
   and EN/DE browser checks at 320/1440 px, covering unreachable zero frames,
   stale readings, disabled polling and connection loss. Existing EN/DE
   event/alarm/display implementation is unchanged by this follow-up.
   PR #37 remains unmerged; no remote CI result is asserted. Tariff bands remain
   presentation-only.
6. **CMT2300A hardware follow-up:** refreshed PR #31 is merged. Local integration validation: all 36 native test executables and the control-surface guard passed, including PSRAM allocation/JSON failure recovery and CMT receive-flag handling. Production configuration compatibility and firmware channel/provenance tests passed; Hoymiles and changed wrapper/test sources passed cpplint with the repository filters. Source parity, hardening ancestry/scope and `git diff --check` passed. The `generic_esp32s3_usb_quad_psram` and `generic_esp32s3_usb` firmware builds passed with the existing embedded web assets; the frontend hook was skipped using `GITHUB_ACTIONS=true`. GitHub Actions were not queried or awaited.

Physical CMT validation remains limited to previously tested behavior; no new hardware validation occurred for the upstream refresh. Exercise reception/recovery and module compatibility on the combined build. Address later failures with rollback or targeted fixes; source/build checks do not establish RF reliability.
7. **Deferred economic policy expansion:** risk calibration, richer efficiency/aging models and recurring target/reserve schedules after real dispatch behavior is validated. PR #34 already implements marginal wear, terminal valuation and monetary hysteresis. Avoid a generic rule-engine DSL.
8. **Economic EV charging:** define product semantics first, then add an EVSE optimizer/dispatch policy through the existing semantic actuator and permission model. No automatic economic EVSE producer exists yet.
9. **Optional inverter firmware-update research:** reverse-engineered Hoymiles update transport exists in historical community work, but this is explicitly deferred and is not part of current `hems`. Any future implementation must be isolated, fail-closed on compatibility and physically validated before promotion.
10. **Polish, documentation and release.**

Identity/recovery work remains optional unless physical testing proves a concrete operational need. There is no architectural mega-milestone before useful operation.

## Integrated work and validation boundaries

### Runtime memory stability (PR #35 merged)

PSRAM is the expected Full-HEMS tier; existing no-PSRAM environments stay as
compatibility builds. Device A (Fusion Board v2.1, ESP32-S3, 8 MB flash, physically
2 MB quad PSRAM) showed no-PSRAM INTERNAL minimum about 5.8 kB during fresh-boot
HTTPS, later 320 B, 28 failed allocations in one long run, two panic resets and
HTTPS largest INTERNAL block roughly 7.7 kB: unacceptable pressure/panics. With
2 MB PSRAM enabled on the same hardware/configuration, initial cycles showed about
51.9 kB INTERNAL minimum, 47–51 kB largest INTERNAL during Forecast.Solar HTTPS,
active PSRAM use and no failed allocations; worker historical free stack about
3.4 kB. Subsequently, PR #35 was physically exercised on Device A with 2 MB PSRAM
for approximately 21 hours without allocation failures, unexpected resets, heap
drift, fragmentation drift or critical stack loss. This supplied validation is
accepted as sufficient for the current project stage, without asserting every
provider/network scenario or indefinite stability.

Merged PR #35 implements explicit PSRAM placement for dynamic bounded
Series/planner data and provider JSON; correlated PSRAM/INTERNAL history; per-plane
scratch release before later HTTPS; bad_alloc-only request recovery; and a bounded
256-byte streaming reader. DP and task stacks remain unchanged. Keep the 512-byte
global malloc fallback. TLS root-set reduction has no measured target benefit and
is deferred; INTERNAL reserve tuning also lacks measured justification and remains
unchanged. No no-PSRAM environment is removed. Software/build evidence and remaining
hardware gates are separated in [runtime memory stability](HEMS_Runtime_Memory_Stability.md).
PR #34 inherits this runtime base: expanded Result/Plan, DP workspace and Series use the hardened allocator; its optional learner snapshot now does too. The fixed retained learner remains static and bounded. No INTERNAL spill occurs when PSRAM is exhausted; no-PSRAM builds retain 8-bit-heap compatibility.

Local validation recorded for **PR #35**: all 35 host test executables
and the control-surface guard passed; `generic_esp32s3_usb`,
`generic_esp32s3_usb_octal_psram` and `generic_esp32s3_usb_quad_psram` firmware builds
passed. Quad compile/link delta against `693eafe6`: +832 B reported RAM and
+2,400 B reported flash. These figures are not runtime heap/physical stability
measurements. The later 21-hour Device A run supplies the stage-accepted physical
validation; implementation details and investigation limits are in [runtime memory stability](HEMS_Runtime_Memory_Stability.md).

### Coordinated energy management (PR #34 merged)

PR #34 was updated on its existing branch and integrated directly into `hems`
after PRs #35, #31 and #36. The bounded global DP, economic degradation/terminal/
hysteresis model, load learning and coordinated storage/DPL behavior are retained;
compatibility work is limited to memory placement and current-state documentation.
DPL remains the sole coordinated Hoymiles writer, and existing permission, mode,
manual-pause, Protection and fresh worker authorization gates remain authoritative.

[Energy management](HEMS_Energy_Management.md) records configuration, conservative
limits, historical resource budgets and current local integration validation.
Implementation is merged; physical Marstek, WARP, optimizer/dispatch, export-control
and combined-build runtime validation remains pending. PR #35's accepted Device A
run and earlier CMT evidence must not be attributed to PR #34.

Local PR #34 integration validation passed all 37 native executables and the
control-surface guard, focused allocation-failure/retry regressions, production
configuration/catalog/channel/provenance checks, frontend tests/type-check/build,
all three intercepted-API browser suites and focused lint/format checks. Both
`generic_esp32s3_usb_quad_psram` and `generic_esp32s3_usb` firmware builds passed
with freshly built web assets. [Detailed results and resource limits](HEMS_Energy_Management.md#hardened-hems-integration-2026-10-28)
remain separate from pending physical validation; GitHub Actions were not polled.

### CMT2300A radio rework (PR #31 merged)

PR #31 compares the previously ported upstream `96f109311b69457fc27f05a01d82091f821f26b2` with current [OpenDTU PR #3296](https://github.com/tbnobody/OpenDTU/pull/3296) head `85841577d4b55493732519edf021895b3353e759`. All five changed CMT files are adapted: receive-state detection and preamble-flag consumption, 3,000 µs wait / 100,000 µs sync hold, packet-drain timer reset and removal of predicted RX-ring state/following.

Recovery/probing, acknowledged channel changes, heard/link state and known line-A single-channel behavior remain. HEMS/OnBattery polling units, CMT-only RF-info traffic, stricter duplicate comparison, DEBUG diagnostics, existing country/radio setup and PR #35 hardening are retained. NRF source remains unchanged, with default-compatible shared abstractions.

Physical CMT validation remains limited to previously tested behavior. No new hardware validation occurred for this refresh; the accepted Device A runtime run applies to PR #35. Refreshed RX/recovery and wider module/RF compatibility remain hardware follow-up work, not a claim of RF reliability.

## Repository cleanup and deferred EV work

Merged PR #36 supersedes PR #32 with the current-`hems` cleanup. The
[hardware validation runbook](HEMS_Hardware_Validation.md), heap-history support
export/regression and binding state-maintenance instructions are salvaged; its
stale status rewrites and no-PSRAM reference are deliberately excluded. The runbook
uses the Full-HEMS PSRAM target and records pending control/dispatch gates; adding
procedures does not complete physical validation. The dispatch contract's obsolete
Draft label is removed because PR #25 is already merged.

PR #33 is deferred/superseded without merge or rebase. Keep
`feature/hems-ev-shadow-simulation` and its [immutable shadow-planning design](https://github.com/dawid-woitaschek/OpenDTU-HEMS/blob/ac771b02266f9c0395f86519a660f845fbd2c125/docs/HEMS_Economic_EV_Charging.md)
for future Economic EV Charging. Its obsolete PR #32 baseline (`59e5b22d`) and
overlap with PR #34's optimizer/service/API/UI/snapshot areas prevent carrying it
forward now. Future EV work must reassess intent/deadline semantics, residual-PV
and storage conflicts, bounded planning, deterministic scenarios and pagination
against current control contracts and PSRAM budgets; selectively reimplement useful
parts rather than replaying stale state text. No economic EVSE producer is installed
in `hems`. PR #33 remains untouched by PR #34 integration.

## Verification and resource discipline

Focused native tests and frontend checks may be used during development. GitHub CI owns the complete firmware matrix and PR firmware-channel publication. Do not waste development time polling remote CI; evaluate results when they are available and act on failures then.

Before recommending flashing or promotion, verify the exact tested source/merge tree, all HEMS targets including `generic_esp32s3_usb`, manifest SHA256/size and normal OTA image. Physical validation must remain distinct from software/CI validation.

The support snapshot is a support/validation transport, not a logging subsystem: it is generated only on demand, secrets are always redacted, network/location data are opt-in, and no persistent diagnostic history is created on the device.

The Full-HEMS acceptance target is now Device A with `generic_esp32s3_usb_quad_psram`; no-PSRAM `generic_esp32s3_usb` remains a compatibility build. PR #29/#30 historically added 960 bytes static DRAM with no PSRAM requirement. Merged PR #35 retains fixed, bounded diagnostics and introduces explicit PSRAM placement without periodic restart, TLS weakening, or TCP/IP/AsyncTCP stack redesign. Its approximately 21-hour Device A PSRAM run is accepted for this stage. Broader scenarios and the combined refreshed CMT behavior remain separate validation work.

The separate intermittent inbound-new-connection investigation remains unproven as a HEMS issue. It does not block the product model.
