# OpenDTU-HEMS current status

Snapshot: 2026-10-28. Product branch: `hems`. The promoted HEMS line includes foundation/productization PR #23, policy PR #24, economic storage dispatch PR #25, modernization/provider/profile work PR #27, promotion PR #28, memory/TLS hardening PR #29 and promotion PR #30, plus **merged PR #35 runtime/PSRAM hardening, refreshed PR #31 CMT2300A rework, PR #36 cleanup and PR #34 coordinated energy management**. PR #35 merged at `f54da01e5fb5086fe5fdf448f7c4dd2e11ecdc3c`; PR #31 was rebased onto that hardened base.

PR #26 was superseded by #27. PR #34 was rebased onto hardened `hems` head `49a2526b` (including PRs #35, #31 and #36) and merged into `hems`. Its implementation is integrated; physical optimizer, coordination and dispatch validation remains pending.

## Usable control model

| Global mode | Behavior |
|---|---|
| Disabled | Automatic control off; existing disabled polling semantics preserved |
| Monitor | Telemetry, proposed policy decisions, manual semantic controls for writable targets |
| Active | Monitor features plus automatic policies for writable targets |

Independent **Storage control** and **EVSE control** permissions select **Monitor only** / **HEMS control**. Missing permissions default Monitor only. Both Monitor means observation only even in Active; either mixed configuration is supported. Manual commands do not require Active. Manual storage commands durably pause opted-in economic automation until explicit authenticated Resume; EV Protection retains priority.

Normal readiness requires configured/enabled device/profile, target permission, reachability, fresh valid telemetry, supported bounded operation and no conflicting worker/command. Generation checks, ordered bounded writes and readback remain. Manual and automatic commands share the same mailbox/worker/client and semantic actuator path. No raw Modbus write API exists.

Durable recovery journals, ownership reconstruction, attestation and temporary-session authorization are not part of normal actuation. Old `hems-recovery-*` files remain ignored and can be inspected/deleted using authenticated File Management. Known RAM ownership gets one best-effort old-endpoint release on permission/config changes; failure is diagnostic, then normal operation continues.

## Active EV protection

EV Protection defaults on; opted-in coordinated settings can explicitly disable it. When enabled, fresh EV charging immediately proposes discharge protection. Protection clears only after 120 seconds of continuously fresh not-charging observations; resumed charging cancels debounce, and unknown/stale telemetry cannot clear it.

Active plus Storage HEMS control rejects new discharge commands while allowing charge commands. Semantic Stop is sent only for fresh observed storage discharge; charging and idle storage are left alone. After debounce, release is attempted only after an actual protection intervention that has not been superseded by charging or a later manual operating command. When coordination owns storage, protection hands Stop custody to Flow without a Release that could restore autonomous export. Monitor-only EVSE telemetry remains fully usable. Proposed versus actionable constraints and actual command results are observable. No automatic EVSE economic policy is installed.

## Economic optimizer and storage dispatch

The bounded global DP optimizer uses exact numeric prices, explicit efficiencies, power/SoC bounds, optional usable PV and bounded terminal inventory valuation. Tariff cheap/normal/expensive categories are presentation-only and never drive control logic.

Economic storage dispatch is implemented as a separate explicit default-off opt-in. It consumes only the authoritative current optimizer interval and issues semantic storage commands through the same readiness, generation, permission, worker, mailbox, profile-plan and readback paths used elsewhere.

Manual storage commands persist a reboot-safe pause before queueing. Only authenticated explicit Resume clears that pause and requests fresh planning. Economic command ownership and cleanup remain source-specific; reboot reconstructs no ownership or plan. Software validation covers current-slot authority, revocation, retry bounds, stale/unreachable conditions, manual precedence, EV Protection precedence and semantic-only actuation. Physical charging/discharging behavior remains to be validated on the actual Marstek installation.

## Merged PR #34 - coordinated energy management

The existing bounded global DP (`T ≤ 256`, `S ≤ 54`, `O(T × S²)`) now includes
independent PV/battery-export permissions and grid-export limits, local-first /
economic / export-first preferences, marginal EFC-reference degradation costs,
an optional conservative throughput margin, bounded terminal inventory valuation
and monetary hysteresis comparing feasible alternatives on the same fresh inputs.
Optional seven-day household-load learning uses 15-minute bins, measured EV-power
exclusion, coverage reporting and an explicit constant fallback. Checksummed history
checkpoints remain bounded to at most once per six hours on the existing worker.

DPL remains the single coordinated Hoymiles writer. The expiring measured-flow
envelope, device/global power ceilings, fresh worker authorization and manual /
Protection ownership checks govern execution. Coordination and automatic learning
default off; Active, Storage HEMS control and economic dispatch remain separate
opt-ins. Compensation does not authorize battery export under coordination. Unknown
wear is disclosed; active coordinated economics requires estimated or explicitly
configured wear. Disable old WARP battery-writing rules before making HEMS the
battery controller; WARP EV control can remain active.

The expanded Result (including Plan), DP workspace, Series and optional learning
snapshot use `hems/Memory.h`: PSRAM when present, no INTERNAL spill on PSRAM
exhaustion, and ordinary 8-bit heap on no-PSRAM compatibility builds. The fixed
2,160-byte retained learner stays in the service's static storage, like retained
provider Series. Constant-load mode allocates no learning snapshot. Stable Result
publication, scratch release before provider TLS, task stacks, provider bounds,
verified TLS and the refreshed CMT implementation are preserved. No optimizer
redesign, new worker or Economic EV Charging implementation is included.

See [energy-management behavior and integration evidence](HEMS_Energy_Management.md).
Physical Marstek/WARP behavior, DPL/export transients, telemetry alignment, learned
load and optimizer timing/resources remain pending on the combined PSRAM build.
The accepted PR #35 run does not validate these later changes.

## Product UI

The unified WebUI and its focused human-centered UX/IA cleanup are **unmerged
work on `redesign-v1`, draft [PR #37](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/37)**,
based on `hems` commit `6b45ebcd`. This is not a release or merge claim.

- One Overview answers current energy flow, price, PV outlook and the next
  battery action. Each current fact appears once. Price/forecast charts and the
  detailed battery schedule, economics and assumptions are expandable. EV
  protection appears as concise relevant status instead of a large duplicated
  Automation card. `/hems` and `/info/hems` still redirect to `/`.
- The persistent sidebar is authoritative, with parent/child selection for routed
  subpages, restrained icons and a mobile menu. Inverters has Overview and
  Configuration children; System status/configuration subpages retain their
  context. The redundant horizontal Energy Management tabs are removed.
- Plant **Solar Charger / Solarladeregler** (`/plant/solar-charger`) owns solar
  charge-controller telemetry and connection settings. It does not display
  normalized total PV as device-specific power. `/plant/solar` and
  `/settings/solarcharger` redirect here. Total PV remains Solar on Overview.
- `/plant/inverters` is a concise per-inverter overview: reachability/data age,
  AC output, confirmed limit, temperature and recorded events where available.
  Event counts are explicitly historical, not claims about current active faults.
  The existing API exposes DPL's last aggregate output target, not a per-inverter
  allocation; the UI identifies that distinction. Detailed channels, radio
  statistics, Grid Profile, event logs and manual actions remain in `/live`,
  with inverter-specific shortcuts and a separate Configuration destination.
- Power Control / DPL configuration belongs to `/energy/power-control`.
  `/plant/inverters/power-limit` and `/settings/powerlimiter` redirect there.
  Physical battery/wallbox connections, profile binding and scoped manual controls
  remain under their devices; imported/repository profiles remain under
  Integrations. Planning preserves all existing API payloads, modes, permissions,
  control types, UTC deadlines and the separate execution opt-in. Readiness and
  deep economic interpretation belong to Diagnostics rather than duplicated
  settings summaries. Disabled PV planes keep their unused fields collapsed;
  azimuth help belongs to the angle label. Overview plan summaries show actions
  and times; step prices, predicted SoC and reasons remain in the expanded plan.
- App owns exactly one `/livedata` WebSocket. The persistent header displays
  neutral connecting/unknown, green connected and red disconnected status with
  DE/EN accessible help, without a pulse. Live Data, the inverter overview and
  overview limits consume the shared snapshot. Partial inverter frames preserve
  other devices, ages advance in the browser, and an HTTP refresh cannot replace
  newer live frames while still discovering configuration changes. Aborted
  owners cannot overwrite newer snapshots, and authorization changes do not
  cause a logout/reconnect loop.
  Device reachability is separate from this transport indicator. The inverter
  overview shows `—` for current AC output, temperature and the confirmed limit
  whenever the inverter is unreachable, polling is disabled, data is older than
  60 seconds or measurements are otherwise unverified. Cached zero or nonzero
  readings are not presented as current. Recorded event counts remain explicitly
  historical; technical Live Data may retain explicitly labeled last-known
  readings. This affects presentation only, not backend control/freshness gates.
- Help buttons stay directly beside their field label or section heading,
  including legacy DTU/inverter/DPL hints. Inputs and interactive help are outside
  each other's labels. Help supports hover, keyboard focus, click/tap toggle,
  outside dismissal and Escape, with viewport-safe positioning. Reserve, upper
  SoC, charging, discharge and execution use distinct concise explanations.
- **German and English are the only UI languages.** French assets/messages are
  removed; browser `fr`/other unsupported preferences fall back to English.
  Uploaded language packs cannot extend the UI's language list. Firmware language
  registration ignores unsupported packs, including older uploaded French files.
  Device display settings/migration and event-log presentation likewise support
  EN/DE, with English fallback for old unsupported locales. French display/event
  strings are removed; the 131 alarm identifiers, protocol parsing, German/English
  alarm messages and 13 German/English display format pairs are preserved.
  These changes affect presentation, not HEMS control behavior.
  Obsolete Greek, Spanish, Italian and Polish pack files are removed.
  `lang/README.md` documents the fixed language set and the existing EN/DE
  device-display overrides; uploaded packs do not add selector languages or
  replace bundled WebUI/event/alarm translations.
- Existing branding is unchanged; no replacement product logo was invented.
  No fixture/mock readings are included in the production UI.

The preceding UX/IA pass on **2026-10-28** passed:

- WebUI TypeScript checking (`vue-tsc --build`), production Vite build, source
  ESLint/Prettier checks and `git diff --check`.
- Focused Node tests for shared live-state ownership and refresh/frame ordering,
  stale/unreachable values, aborted-owner isolation, strict locale fallback,
  admin persistence/error handling, HEMS presentation/support snapshots,
  energy-management settings, exact tariff units and profile-catalog integrity.
- `test/test_redesign_browser.mjs` against the served production build at
  `HEMS_BASE_URL=http://127.0.0.1:5174`: English/German primary and System routes
  at 320/768/1440 px, no page overflow/runtime errors, nested active navigation,
  physical-device ownership, attached help placement, keyboard/touch/outside/
  Escape dismissal, one live connection across navigation, last-known values,
  legacy redirects, unsupported locale fallback, unchanged planning/deadline
  payloads and no browsing-triggered device commands. It now runs in the HEMS
  browser regression workflow. Fixture-based screenshots were reviewed on
  desktop/mobile, including the inverter summary and planning forms; German
  long titles and the Console auto-scroll switch now wrap within the viewport.
- Existing HEMS and economic browser regressions at 320/360/768/1440 px:
  dark mode/contrast, keyboard/touch/DST charts, stale/partial/missing data,
  tariff/PV saves, Monitor isolation, execution opt-in, manual pause and explicit
  resume. Expanded details remain covered rather than removing those checks.
- Modernization browser regressions at 360/1440 px: firmware stale/race states,
  import/update/integrity/backend/network failures, manual fallback/export,
  repository imports under Integrations and imported storage/EVSE profile
  selection on the corresponding physical device pages.
- Source integrity review preserved every EN/DE alarm message and display format
  while removing the French columns; this is not a firmware-build claim.

Final cleanup verification on **2026-10-28** passed focused WebUI type checking,
production build, changed-source ESLint/Prettier checks, the shared-state/locale
Node regression and the redesign browser regression in EN/DE at 320/1440 px.
The browser checks distinguish verified current zero readings from unreachable
zero frames, and require `—` for unverified AC output, temperature and limits
after stale data, disabled polling or connection loss. A repository asset guard
rejects unsupported locale packs. No controller, event/alarm parser or display
implementation changed in this follow-up; EN/DE behavior is preserved.

Cleanup validation is software-only and browser requests use intercepted API
fixtures. Physical ESP32/device and live-installation visual validation remain
pending. The full local CI/firmware matrix was not run, and GitHub Actions were
not queried, awaited or monitored. `redesign-v1` / PR #37 remains unmerged.

## Native providers and external data

Energy-Charts pricing uses the current `/v2/price?bzn=DE-LU` contract with strict envelope/time-series validation, DE-LU bidding-zone checks, resolution/timestamp validation, nullable-price handling, provider generation timestamp, bounded parsing and no v1 fallback.

Forecast.Solar support remains key-free and uses the documented estimate endpoint plus the deliberately retained `full=1` behavior needed by the current dense hourly parser. Multi-plane state, quota/backoff diagnostics and atomic site aggregate replacement are implemented. Provider failures retain previously accepted data only while it remains usable under freshness/coverage rules.

Live provider/on-device functional validation remains a separate gate. Desktop/API fixtures and software tests do not prove actual ESP32 DNS/TLS/provider behavior in every field condition.

## Runtime memory and TLS status

PR #29 audited the observed ESP32-S3 TLS allocation failures and found no proven provider memory leak. The key observed failure mode was loss of sufficiently large contiguous INTERNAL heap blocks while total free heap stayed broadly stable. The pinned Arduino 2.0.17 / ESP-IDF 4.4.7 mbedTLS binary requires large contiguous record buffers, making largest-block availability operationally important.

The merged hardening reduces avoidable allocation overlap, keeps the retained optimizer Result at a stable address, releases validation/scratch allocations earlier and adds bounded fixed-storage heap diagnostics. That historical pass introduced no PSRAM requirement, periodic restart, TLS weakening, global mbedTLS/lwIP redesign or large new task stack.

PR #30 promoted this work to `hems` after **more than 24 hours of physical `generic_esp32s3_usb` ESP32-S3 validation without PSRAM**, including successful Forecast.Solar TLS cycles on both planes and recovery of the largest INTERNAL free block after TLS activity. This validates the memory/TLS hardening behavior observed during that run; it does not establish every provider/network scenario or prove absence of all future fragmentation issues.

The PR #29 resource delta for `generic_esp32s3_usb` was +960 B static DRAM and +4,460 B reported flash over its baseline, with no PSRAM dependency. Fixed diagnostics include a preserved boot baseline, bounded heap checkpoints and allocation-failure records.

Later physical evidence on **Device A** (ESP32-S3, Fusion Board v2.1, 8 MB flash,
physically 2 MB quad PSRAM) supersedes no-PSRAM acceptance: fresh-boot INTERNAL
minimum about 5.8 kB during HTTPS, later as low as 320 B, 28 failed allocations in
one long run, two unexpected panic resets, and HTTPS largest INTERNAL block roughly
7.7 kB. No-PSRAM Full HEMS has shown unacceptable pressure and panics on this device.
After enabling its 2 MB PSRAM on the same hardware/configuration, initial cycles
showed INTERNAL minimum about 51.9 kB, largest INTERNAL block around 47–51 kB during
Forecast.Solar HTTPS, active PSRAM use and no failed allocations. Worker historical
free stack remained about 3.4 kB. Subsequently, **PR #35 was physically exercised on
Device A with 2 MB PSRAM for approximately 21 hours**, without allocation failures,
unexpected resets, heap drift, fragmentation drift or critical stack loss. This
user-supplied validation is accepted as sufficient for the current project stage;
it does not establish every provider/network scenario or future stability.

**PSRAM is the expected Full-HEMS memory tier.** No-PSRAM environments remain for
compatibility; they are no longer the Full-HEMS design/acceptance target.

Merged **PR #35** adds correlated PSRAM/INTERNAL bounded diagnostics,
explicit PSRAM placement for dynamic Series/planner data and provider JSON, earlier
per-plane scratch release, allocation-only native request exception recovery and a
256-byte streaming reader. It retains the bounded global DP, stacks, 512-byte malloc
fallback, TLS roots/verification and current INTERNAL reserve configuration. With
PSRAM present, explicit allocations fail gracefully instead of consuming INTERNAL
on PSRAM exhaustion. See [implementation, investigations and validation boundaries](HEMS_Runtime_Memory_Stability.md).

Local validation recorded for **PR #35**: all 35 host test executables
and the control-surface guard passed; `generic_esp32s3_usb`,
`generic_esp32s3_usb_octal_psram` and `generic_esp32s3_usb_quad_psram` firmware builds
passed. Quad compile/link delta against `693eafe6`: +832 B reported RAM and
+2,400 B reported flash. These figures are not runtime heap/physical stability
measurements. The later 21-hour Device A run supplies the stage-accepted physical
validation; implementation details and investigation limits are in [runtime memory stability](HEMS_Runtime_Memory_Stability.md).

## Validation and firmware

Local PR #34 integration validation passed all 37 native executables and the
control-surface guard, focused allocation-failure/retry regressions, production
configuration/catalog/channel/provenance checks, frontend tests/type-check/build,
all three intercepted-API browser suites and focused lint/format checks. Both
`generic_esp32s3_usb_quad_psram` and `generic_esp32s3_usb` firmware builds passed
with freshly built web assets. [Detailed results and resource limits](HEMS_Energy_Management.md#hardened-hems-integration-2026-10-28)
remain separate from pending physical validation; GitHub Actions were not polled.

Software validation accumulated through the merged milestones includes deterministic native controller/policy/provider/mailbox/executor coverage, all four permission combinations, Monitor manual commands, Active-only automatic commands, EV Protection/debounce, dispatch authority/revocation/retry/pause behavior, parser/catalog/planner/readback tests, frontend tests/builds and the firmware matrices documented in the respective PRs.

Full-HEMS runtime reference: Device A, Fusion Board v2.1, ESP32-S3, 8 MB flash, 2 MB quad PSRAM, `generic_esp32s3_usb_quad_psram`. Existing no-PSRAM builds remain compatibility targets. Shield v1.1 remains the control-validation reference.

**Physically validated on current promoted line:**

- PR #35 on Device A with 2 MB PSRAM for approximately 21 hours, without allocation failures, unexpected resets, heap/fragmentation drift or critical stack loss; accepted for the current stage.
- Historical PR #30 runtime memory/TLS run over >24 h on ESP32-S3 `generic_esp32s3_usb` without PSRAM; later Device A failures prevent treating this as general no-PSRAM stability acceptance.
- Successful Forecast.Solar TLS cycles on both configured planes during that run.
- Recovery of largest INTERNAL free block after TLS activity during that run.

**Still not physically validated / still open:**

- Runtime scenarios beyond the supplied approximately 21-hour Device A run.
- Merged PR #34 energy-management behavior: learned-load fallback, economic/terminal/hysteresis decisions, measured-flow coordination, DPL zero/lower limits and real export transients; runtime resources and responsiveness on the combined PSRAM build.
- WARP3 current/stop/release lifecycle end to end.
- Marstek V148 acquire/charge/discharge/Stop/Release lifecycle, actual sign/scale and persistence/watchdog behavior.
- EV Protection end to end against the physical WARP3/Marstek combination.
- Full economic dispatch causing verified physical charge/discharge behavior.
- Representative Energy-Charts v2 live ESP32 runs and broader provider/TLS resource behavior under varied field conditions.
- Full Forecast.Solar scheduling/quota behavior and any intermittent East-plane issue reproduction.
- Refreshed PR #31 CMT2300A RX/recovery beyond the previously tested behavior.

## Merged PR #31 - CMT2300A radio recovery / RX hopping

PR #31 was refreshed from its previously ported upstream revision `96f109311b69457fc27f05a01d82091f821f26b2` to [OpenDTU PR #3296](https://github.com/tbnobody/OpenDTU/pull/3296) head `85841577d4b55493732519edf021895b3353e759`, rebased onto hardened `hems` containing PR #35 and merged.

The upstream delta touches five CMT files. Receive-state-driven hopping replaces predicted RX-ring following: preamble detection clears its consumed flag and restarts the 3,000 µs channel wait; sync/CRC/packet evidence holds the channel for up to 100,000 µs. Packet draining resets the hold/wait, and obsolete per-inverter ring prediction is removed. Recovery, legal-band probing, channel-change acknowledgements, module identification and heard/link state remain. Known line-A modules retain single-channel reception.

HEMS/OnBattery polling and millisecond timing, stricter channel-change duplicate comparison, CMT-only RF-info polling, DEBUG diagnostics, existing country/radio setup and PR #35 runtime hardening are preserved. NRF source behavior remains unchanged; shared response filtering defaults off for the existing NRF caller.

Local integration validation: all 36 native test executables and the control-surface guard passed, including PSRAM allocation/JSON failure recovery and CMT receive-flag handling. Production configuration compatibility and firmware channel/provenance tests passed; Hoymiles and changed wrapper/test sources passed cpplint with the repository filters. Source parity, hardening ancestry/scope and `git diff --check` passed. The `generic_esp32s3_usb_quad_psram` and `generic_esp32s3_usb` firmware builds passed with the existing embedded web assets; the frontend hook was skipped using `GITHUB_ACTIONS=true`. GitHub Actions were not queried or awaited.

Physical CMT validation remains limited to the previously tested behavior. **No new hardware validation occurred for this refresh**; reception/recovery with the refreshed hopping logic and broader module/RF reliability remain unverified physically. The Device A runtime run validates PR #35, not these new CMT changes. Later hardware failures can be handled with rollback or targeted fixes.

## Recommended next validation sequence

Follow the [hardware validation runbook](HEMS_Hardware_Validation.md) for procedures
and result records. All control/dispatch cases remain pending until evidenced.

1. Continue observing the combined hardened/CMT/energy-management build on Device A using `generic_esp32s3_usb_quad_psram`. Record exact source/build identity and INTERNAL/PSRAM/stack diagnostics; validate refreshed CMT reception and recovery separately from the accepted PR #35 runtime run.
2. Validate WARP3 semantic current/stop/release and Marstek bounded charge/discharge/stop/SoC/release against actual physical behavior, not just register readback.
3. Exercise all storage/EVSE permission combinations in Monitor and Active, including communication interruption and recovery.
4. Validate EV Protection end to end with real charging and storage discharge states, including the 120-second fresh-false release debounce.
5. Enable economic dispatch/coordination only after the semantic storage path is physically trusted; compare plan, command, readback and measured battery/grid/PV behavior. Exercise zero/changing export limits, stale data, actuator failure, EV ownership handoff and strict PV-only startup; measure planning latency, TLS headroom and DPL responsiveness with learning enabled.
6. Capture support snapshots and heap history around representative provider/dispatch runs. Use equivalent quiet-period comparisons when judging fragmentation or retention.
7. Validate live Energy-Charts and Forecast.Solar provider behavior on-device without conflating successful desktop fixtures with ESP32 field validation.

## Repository cleanup and deferred work

Merged PR #36 supersedes PR #32 on current `hems`: its useful hardware runbook,
on-demand heap-history export/regression and binding project-state maintenance
rule are retained. Its old status/roadmap rewrites and no-PSRAM reference are
obsolete after merged PRs #35/#31 and are deliberately excluded. The dispatch
contract now identifies merged PR #25 instead of calling it Draft. No new physical
validation is claimed.

PR #33 is deferred/superseded, not merged or rebased. Preserve its
`feature/hems-ev-shadow-simulation` branch and [design at the recorded head](https://github.com/dawid-woitaschek/OpenDTU-HEMS/blob/ac771b02266f9c0395f86519a660f845fbd2c125/docs/HEMS_Economic_EV_Charging.md)
as future Economic EV Charging history. It depends on obsolete PR #32 head
`59e5b22d` and overlaps optimizer/service/API/UI/snapshot areas changed by merged PR #34. Future work must reassess intent/deadline, residual-PV/storage
conflicts, bounded planning, deterministic scenarios and snapshot pagination against
the current architecture and PSRAM budget. Its shadow planner is not current
`hems` behavior and it implemented no automatic economic EVSE dispatch. Do not
reuse its old baseline/hardware/status claims or blindly replay the four feature
commits (`fc5dc2d6` through `ac771b02`). PR #33 was not touched by PR #34 integration.

## Documentation invariant

`docs/HEMS_Roadmap.md` and this file are authoritative project-state documents. Every HEMS-relevant PR must check both. Any implementation, validation, hardware status, open work, branch/PR, firmware-channel or known-risk change must update both files in the same PR when the documented project state changes. Before merge or promotion to `hems`, both documents must describe the resulting `hems` state accurately. Unmerged or physically unvalidated work must remain explicitly labelled as such.

See [Roadmap](HEMS_Roadmap.md), [control model](HEMS_CONTROL.md), [policies](HEMS_Policies.md), [dispatch](HEMS_Economic_Dispatch.md), [energy data](HEMS_Energy_Data.md) and [native energy/optimizer](HEMS_Native_Energy.md).
