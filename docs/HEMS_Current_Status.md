# OpenDTU-HEMS current status

Snapshot: 2026-10-23. Product branch: `hems`. The promoted HEMS line currently includes foundation/productization PR #23, policy PR #24, economic storage dispatch PR #25, modernization/provider/profile work PR #27, promotion PR #28, memory/TLS hardening PR #29 and promotion PR #30. Current `hems` source checked for the energy-management integration is `693eafe60fb2538ec11019dc0875737eb7e0495c`; the promoted product baseline before the documentation refresh was `617ef19a3af6e64b81a349a867c16facaae6aa04`.

PR #26 was superseded by #27. Open PR #31 (`feature/cmt-radio-rework`) targets `hems` but is **not merged and not physically validated**. Draft PR #34 (`codex/hems-energy-management` -> `hems-energy-management`) is a separate integration line based on `hems` SHA `693eafe60fb2538ec11019dc0875737eb7e0495c`; it is **not merged, not physically validated and not current product behavior**.

## Usable control model in current `hems`

| Global mode | Behavior |
|---|---|
| Disabled | Automatic control off; existing disabled polling semantics preserved |
| Monitor | Telemetry, proposed policy decisions, manual semantic controls for writable targets |
| Active | Monitor features plus automatic policies for writable targets |

Independent **Storage control** and **EVSE control** permissions select **Monitor only** / **HEMS control**. Missing permissions default Monitor only. Both Monitor means observation only even in Active; either mixed configuration is supported. Manual commands do not require Active. Manual storage commands durably pause opted-in economic automation until explicit authenticated Resume; EV Protection retains priority.

Normal readiness requires configured/enabled device/profile, target permission, reachability, fresh valid telemetry, supported bounded operation and no conflicting worker/command. Generation checks, ordered bounded writes and readback remain. Manual and automatic commands share the same mailbox/worker/client and semantic actuator path. No raw Modbus write API exists.

Durable recovery journals, ownership reconstruction, attestation and temporary-session authorization are not part of normal actuation. Old `hems-recovery-*` files remain ignored and can be inspected/deleted using authenticated File Management. Known RAM ownership gets one best-effort old-endpoint release on permission/config changes; failure is diagnostic, then normal operation continues.

## Active EV protection in current `hems`

Fresh EV charging immediately proposes discharge protection. Protection clears only after 120 seconds of continuously fresh not-charging observations; resumed charging cancels debounce, and unknown/stale telemetry cannot clear it.

Active plus Storage HEMS control rejects new discharge commands while allowing charge commands. Semantic Stop is sent only for fresh observed storage discharge; charging and idle storage are left alone. After debounce, release is attempted only after an actual protection intervention that has not been superseded by charging or a later manual operating command. Monitor-only EVSE telemetry remains fully usable. Proposed versus actionable constraints and actual command results are observable. No automatic EVSE economic policy is installed.

## Economic optimizer and storage dispatch in current `hems`

The promoted bounded DP optimizer uses exact numeric prices, explicit efficiencies, power/SoC bounds, optional usable PV and the existing terminal reserve. Tariff cheap/normal/expensive categories are presentation-only and never drive control logic. The promoted load model remains the configured constant base load; the new learned per-slot load profile and terminal inventory valuation described below exist only on PR #34's integration line.

Economic storage dispatch is implemented as a separate explicit default-off opt-in. It consumes only the authoritative current optimizer interval and issues semantic storage commands through the same readiness, generation, permission, worker, mailbox, profile-plan and readback paths used elsewhere.

Manual storage commands persist a reboot-safe pause before queueing. Only authenticated explicit Resume clears that pause and requests fresh planning. Economic command ownership and cleanup remain source-specific; reboot reconstructs no ownership or plan. Software validation covers current-slot authority, revocation, retry bounds, stale/unreachable conditions, manual precedence, EV Protection precedence and semantic-only actuation. Physical charging/discharging behavior remains to be validated on the actual Marstek installation.

## Draft PR #34 - coordinated energy-management integration

PR #34 targets the separate `hems-energy-management` integration branch from `codex/hems-energy-management`. It preserves the existing bounded DP over time slots and stored-energy states while extending both the feasible energy-flow model and the economic objective. None of the behavior in this section is part of current `hems` until reviewed, physically validated and deliberately promoted.

### Planning and economics

- Separate configuration models physical AC power limits, DPL regulation target, maximum permitted net grid export, PV-export permission and priority, battery-export permission, export compensation, grid-charging permission and EV-related discharge protection.
- Battery-export permission is independent from export compensation. When battery export is forbidden, planned/runtime discharge is constrained to residual permitted local consumption and must not intentionally create or increase export.
- The DP accepts per-slot household-load estimates without adding an energy-state dimension. Automatic learning is optional and default-off; constant configured load remains an explicit fallback.
- The local load learner uses bounded 15-minute energy aggregates, requires comparable valid observations, tracks coverage and day type, treats missing data as unknown and omits EV-charging intervals when no reliable separate EV power measurement exists. WARP Smart allowed current is not treated as measured charging power.
- Marginal cycle degradation is an explicit primary DP cost. EFC-reference throughput is half the sum of absolute internal stored-energy changes; dimensionless EFC uses a fixed documented reference capacity rather than the selected operating SoC window. AC conversion losses and PV opportunity costs remain separate.
- An independent optional throughput margin is modeled as a conservative margin, not claimed as calibrated forecast risk.
- The moving start-energy terminal floor is replaced on the integration line by bounded terminal inventory valuation over the existing energy states. Available covered future tariffs are used when present; documented fallback assumptions are used when future coverage is incomplete rather than fabricating perfect future data.
- Monetary plan-change hysteresis compares feasible alternatives from the same current battery state under the same fresh inputs and terminal valuation. It does not introduce a fixed price spread, local minimum-profit threshold or whole-plan minimum-profit gate, and hard protections remain immediate.
- Throughput remains a secondary deterministic tie-breaker after modeled monetary cost.

### Coordinated execution

- DPL remains the sole coordinated Hoymiles writer; PR #34 does not add a second inverter worker or synchronous network operation to the DPL loop.
- A bounded, expiring coordination envelope constrains the existing DPL output path, including MQTT passthrough while coordination owns the actuator path.
- DPL may account for battery charging only after fresh storage telemetry confirms that charging is actually occurring. A requested charge command alone is not treated as an established electrical load.
- Storage commands are bounded against fresh measured grid, PV and storage flows, with authorization rechecked before writes. Coordinated release/target paths that could restore autonomous battery export are rejected while coordination requires control.
- Missing/stale measurements, missing/failed actuators, unsupported topology or lost authorization report coordinated enforcement unavailable and fall back conservatively. The battery's own export setting is not treated as a system-level net-grid-export guarantee.
- No instantaneous or transient-free export-prevention guarantee is claimed for networked control.

### Configuration and UI

- New coordination and automatic learning require explicit opt-in; migration grants no new actuator permission or automatic takeover.
- Battery export defaults forbidden, EV protection defaults enabled and battery-wear cost defaults unknown/unmodeled rather than verified zero. Estimated and explicitly configured zero coefficients remain distinguishable.
- Normal settings expose operating preference, export permissions, net export limit, EV protection and automatic versus fixed load estimation. Detailed wear/margin, terminal and hysteresis settings remain Advanced.
- A reference preset selects PV-first use, no battery export, EV protection, automatic learning and grid charging without inventing device ratings, degradation coefficients or control permissions. Existing WARP battery-writing rules must be disabled before HEMS becomes the battery controller; WARP EV control may remain active.
- English, German and French presentation is included on the integration branch.

### PR #34 software validation and resource measurements

The PR currently documents local software/build validation, not device validation:

- 35 native test executables passed, including production planner/service/controller/provider/mailbox/executor paths.
- Focused fixtures cover PV/load/battery coordination, battery-induced export, zero/changing export limits, positive export compensation with battery export forbidden, stale data and actuator failure, EV-learning gaps, degradation/negative-price/PV-opportunity accounting, repeated replanning and fresh-cost hysteresis behavior.
- Closed-cycle 95%/95% efficiency with 3 ct/EFC-reference-kWh wear rejects the isolated 15->18 ct/kWh cycle and accepts 15->35 ct/kWh.
- Forecast fixtures compare learned profile, fixed fallback and the existing WARP-style schedule; the documentation explicitly does not claim three days of history are universally superior.
- UI production build/type checks, tests, lint, formatting and intercepted browser checks passed locally.
- A representative `generic_esp32s3_usb`, 8 MB, no-PSRAM firmware build passed locally. No hardware was flashed or operated.
- Reported same-toolchain resource delta versus the PR base: +2,288 B static RAM, +28,860 B firmware flash, +432 B DP workspace, +2,120 B `Plan`, +2,208 B retained result allocation and +7,008 B learning-enabled peak planning object allocations. These figures are compile/layout or host allocation measurements, not live ESP32 heap/stack/latency results.
- Planning complexity remains bounded `O(T x S^2)` with `T <= 256` and `S <= 54`; no PSRAM, cloud service, external database, ML framework, extra computer or new task is required by the integration.

### PR #34 validation still required

Before promotion, validate on the actual reference hardware:

- Marstek V148 register sign/scale, acquire/charge/discharge/Stop/Release semantics, command persistence/watchdog and response latency.
- Freshness and timestamp alignment of grid/PV/storage telemetry, including whether nighttime Hoymiles telemetry is adequate for the required coordination mode.
- Actual DPL zero/lower-output behavior and real import/export transients during charge/discharge changes, sudden load changes and actuator/network failures.
- Runtime heap/largest-free-block behavior, task stack watermarks, planning latency, DPL responsiveness and TLS headroom on the no-PSRAM ESP32-S3.
- End-to-end coordination with WARP EV protection and the real Marstek/Hoymiles/grid-meter topology.

## Product UI in current `hems`

Four peer views use the existing OpenDTU visual language: Overview (`/hems`), Settings (`/hems/settings`), Devices (`/hems/devices`) and Advanced Diagnostics (`/hems/diagnostics`). Legacy aliases remain supported.

Overview shows fresh energy values, effective tariff prices, interactive tariff/PV charts and provider readiness. The generated storage plan is a first-class daily-use surface: current planned action, next action change, merged charge/hold/discharge blocks, expected SoC progression, modeled benefit and compact strategy summary.

Settings owns normal storage-strategy configuration: reserve, maximum desired SoC, grid-charging permission, economic-discharge permission and optional one-time target/deadline. Battery/model parameters remain under collapsed Advanced settings. Economic dispatch remains a separate explicit default-off opt-in.

Devices owns endpoints, bound profiles and expandable manual semantic controls. Repository-backed profile distribution is implemented: the browser retrieves a selected immutable profile, verifies size/SHA-256 and uses authenticated backend import/replacement; backend validation and in-use checks remain authoritative.

Advanced Diagnostics exposes provider/control/system details, paginated optimizer rows and the on-demand browser-only support snapshot `opendtu-hems-diagnostics/v1`. It sequentially combines existing firmware, system, HEMS, tariff, forecast and optimizer APIs. Credentials are always redacted; network/location details remain opt-in. The snapshot is never persisted on LittleFS.

## Native providers and external data

Energy-Charts pricing uses the current `/v2/price?bzn=DE-LU` contract with strict envelope/time-series validation, DE-LU bidding-zone checks, resolution/timestamp validation, nullable-price handling, provider generation timestamp, bounded parsing and no v1 fallback.

Forecast.Solar support remains key-free and uses the documented estimate endpoint plus the deliberately retained `full=1` behavior needed by the current dense hourly parser. Multi-plane state, quota/backoff diagnostics and atomic site aggregate replacement are implemented. Provider failures retain previously accepted data only while it remains usable under freshness/coverage rules.

Live provider/on-device functional validation remains a separate gate. Desktop/API fixtures and software tests do not prove actual ESP32 DNS/TLS/provider behavior in every field condition.

## Runtime memory and TLS status

PR #29 audited the observed ESP32-S3 TLS allocation failures and found no proven provider memory leak. The key observed failure mode was loss of sufficiently large contiguous INTERNAL heap blocks while total free heap stayed broadly stable. The pinned Arduino 2.0.17 / ESP-IDF 4.4.7 mbedTLS binary requires large contiguous record buffers, making largest-block availability operationally important.

The merged hardening reduces avoidable allocation overlap, keeps the retained optimizer Result at a stable address, releases validation/scratch allocations earlier and adds bounded fixed-storage heap diagnostics. No PSRAM requirement, periodic restart, TLS weakening, global mbedTLS/lwIP redesign or large new task stack was introduced.

PR #30 promoted this work to `hems` after **more than 24 hours of physical `generic_esp32s3_usb` ESP32-S3 validation without PSRAM**, including successful Forecast.Solar TLS cycles on both planes and recovery of the largest INTERNAL free block after TLS activity. This validates the memory/TLS hardening behavior observed during that run; it does not establish every provider/network scenario or prove absence of all future fragmentation issues.

The PR #29 resource delta for `generic_esp32s3_usb` was +960 B static DRAM and +4,460 B reported flash over its baseline, with no PSRAM dependency. Fixed diagnostics include a preserved boot baseline, bounded heap checkpoints and allocation-failure records.

## Validation and firmware

Software validation accumulated through the merged milestones includes deterministic native controller/policy/provider/mailbox/executor coverage, all four permission combinations, Monitor manual commands, Active-only automatic commands, EV Protection/debounce, dispatch authority/revocation/retry/pause behavior, parser/catalog/planner/readback tests, frontend tests/builds and the firmware matrices documented in the respective PRs.

Reference hardware remains Fusion Board v2.1 / Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`. The no-PSRAM build remains the acceptance baseline.

**Physically validated on current promoted line:**
- Runtime memory/TLS hardening over >24 h on ESP32-S3 `generic_esp32s3_usb` without PSRAM.
- Successful Forecast.Solar TLS cycles on both configured planes during that run.
- Recovery of largest INTERNAL free block after TLS activity during that run.

**Still not physically validated / still open:**
- WARP3 current/stop/release lifecycle end to end.
- Marstek V148 acquire/charge/discharge/Stop/Release lifecycle, actual sign/scale and persistence/watchdog behavior.
- EV Protection end to end against the physical WARP3/Marstek combination.
- Full economic dispatch causing verified physical charge/discharge behavior.
- PR #34 coordinated net-export enforcement, load-learning behavior, terminal valuation and hysteresis on actual hardware.
- PR #34 runtime heap/stack/latency/DPL responsiveness/TLS headroom and physical export transients.
- Representative Energy-Charts v2 live ESP32 runs and broader provider/TLS resource behavior under varied field conditions.
- Full Forecast.Solar scheduling/quota behavior and any intermittent East-plane issue reproduction.
- PR #31 CMT2300A RF recovery/hopping behavior.

## Open PR #34 - coordinated energy management

PR #34 is a draft against `hems-energy-management` from `codex/hems-energy-management`. It is deliberately isolated from `hems` while the broad planner/runtime/UI integration is reviewed and physically validated. The implementation and local validation described above are not evidence of real Marstek response timing, transient-free grid-export enforcement or live ESP32 resource margins.

## Open PR #31 - CMT2300A radio recovery / RX hopping

PR #31 is open against `hems` from `feature/cmt-radio-rework`; documented head `a9f0592a0fbbfc3adf1324d397272252a64783d0`. It ports current upstream CMT2300A recovery, probing, acknowledged channel changes, per-inverter heard/link state and RX-ring following while preserving HEMS/OnBattery behavior and intentionally avoiding direct NRF-source changes.

The initial six-commit head had passing CI documented in the PR. Later upstream-parity follow-ups were pushed; their CI status is deliberately not asserted here. No hardware validation has been performed. Therefore none of PR #31's RF behavior is part of current `hems` until the PR is reviewed, validated and merged/promoted with both status documents updated in the same change.

## Recommended next validation sequence

1. Review PR #34's planner/runtime ownership and fail-safe behavior before hardware use; keep it on the separate integration line while findings are corrected.
2. On the reference no-PSRAM ESP32-S3, validate Marstek semantic charge/discharge/Stop behavior and actual telemetry sign/scale/latency before relying on coordinated dispatch.
3. Validate coordinated DPL/storage behavior with conservative export limits, including charge-command failure, delayed charge telemetry, battery-induced export, sudden load drops, stale meters and actuator loss. Measure actual grid transients rather than inferring them from command/readback state.
4. Validate WARP3 semantic current/stop/release and EV Protection end to end, then exercise all storage/EVSE permission combinations in Monitor and Active.
5. Compare planned action, issued command, verified readback and measured battery/grid behavior using live tariffs and forecast data. Exercise learned-load fallback/coverage, repeated replanning and monetary hysteresis with fresh comparable inputs.
6. Measure runtime heap/largest free block, stack watermarks, planning latency, DPL responsiveness and TLS headroom around representative coordinated planning/provider cycles.
7. Validate live Energy-Charts and Forecast.Solar provider behavior on-device without conflating successful desktop fixtures with ESP32 field validation.
8. Only after the integration behavior and documentation match physical results, decide whether/how to promote `hems-energy-management` to `hems`; update this file and `HEMS_Roadmap.md` again in that promotion change.

## Documentation invariant

`docs/HEMS_Roadmap.md` and this file are authoritative project-state documents. Every HEMS-relevant PR must check both. Any implementation, validation, hardware status, open work, branch/PR, firmware-channel or known-risk change must update both files in the same PR when the documented project state changes. Before merge or promotion to `hems`, both documents must describe the resulting `hems` state accurately. Unmerged or physically unvalidated work must remain explicitly labelled as such.

See [Roadmap](HEMS_Roadmap.md), [control model](HEMS_CONTROL.md), [policies](HEMS_Policies.md), [dispatch](HEMS_Economic_Dispatch.md), [energy management](HEMS_Energy_Management.md), [energy data](HEMS_Energy_Data.md) and [native energy/optimizer](HEMS_Native_Energy.md).