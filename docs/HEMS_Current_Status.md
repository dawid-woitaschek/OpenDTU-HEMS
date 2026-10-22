# OpenDTU-HEMS current status

Snapshot: 2026-10-22. Product branch: `hems`. The promoted HEMS line currently includes foundation/productization PR #23, policy PR #24, economic storage dispatch PR #25, modernization/provider/profile work PR #27, promotion PR #28, memory/TLS hardening PR #29 and promotion PR #30. Promoted product baseline before this documentation refresh: `617ef19a3af6e64b81a349a867c16facaae6aa04`.

PR #26 was superseded by #27. Open PR #31 (`feature/cmt-radio-rework`) targets `hems` but is **not merged and not physically validated**. It must not be treated as current product behavior.

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

Fresh EV charging immediately proposes discharge protection. Protection clears only after 120 seconds of continuously fresh not-charging observations; resumed charging cancels debounce, and unknown/stale telemetry cannot clear it.

Active plus Storage HEMS control rejects new discharge commands while allowing charge commands. Semantic Stop is sent only for fresh observed storage discharge; charging and idle storage are left alone. After debounce, release is attempted only after an actual protection intervention that has not been superseded by charging or a later manual operating command. Monitor-only EVSE telemetry remains fully usable. Proposed versus actionable constraints and actual command results are observable. No automatic EVSE economic policy is installed.

## Economic optimizer and storage dispatch

The bounded DP optimizer uses exact numeric prices, explicit efficiencies, power/SoC bounds, optional usable PV and terminal reserve. Tariff cheap/normal/expensive categories are presentation-only and never drive control logic.

Economic storage dispatch is implemented as a separate explicit default-off opt-in. It consumes only the authoritative current optimizer interval and issues semantic storage commands through the same readiness, generation, permission, worker, mailbox, profile-plan and readback paths used elsewhere.

Manual storage commands persist a reboot-safe pause before queueing. Only authenticated explicit Resume clears that pause and requests fresh planning. Economic command ownership and cleanup remain source-specific; reboot reconstructs no ownership or plan. Software validation covers current-slot authority, revocation, retry bounds, stale/unreachable conditions, manual precedence, EV Protection precedence and semantic-only actuation. Physical charging/discharging behavior remains to be validated on the actual Marstek installation.

## Product UI

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
- Representative Energy-Charts v2 live ESP32 runs and broader provider/TLS resource behavior under varied field conditions.
- Full Forecast.Solar scheduling/quota behavior and any intermittent East-plane issue reproduction.
- PR #31 CMT2300A RF recovery/hopping behavior.

## Open PR #31 - CMT2300A radio recovery / RX hopping

PR #31 is open against `hems` from `feature/cmt-radio-rework`; documented head `a9f0592a0fbbfc3adf1324d397272252a64783d0`. It ports current upstream CMT2300A recovery, probing, acknowledged channel changes, per-inverter heard/link state and RX-ring following while preserving HEMS/OnBattery behavior and intentionally avoiding direct NRF-source changes.

The initial six-commit head had passing CI documented in the PR. Later upstream-parity follow-ups were pushed; their CI status is deliberately not asserted here. No hardware validation has been performed. Therefore none of PR #31's RF behavior is part of current `hems` until the PR is reviewed, validated and merged/promoted with both status documents updated in the same change.

## Recommended next validation sequence

1. Flash current verified `hems` firmware for `generic_esp32s3_usb` using the normal OTA image, never a factory image for OTA.
2. Validate WARP3 semantic current/stop/release and Marstek bounded charge/discharge/stop/SoC/release against actual physical behavior, not just register readback.
3. Exercise all storage/EVSE permission combinations in Monitor and Active, including communication interruption and recovery.
4. Validate EV Protection end to end with real charging and storage discharge states, including the 120-second fresh-false release debounce.
5. Enable economic dispatch only after the semantic storage path is physically trusted; compare planned action, issued command, verified readback and actual battery behavior.
6. Capture support snapshots and heap history around representative provider/dispatch runs. Use equivalent quiet-period comparisons when judging fragmentation or retention.
7. Validate live Energy-Charts and Forecast.Solar provider behavior on-device without conflating successful desktop fixtures with ESP32 field validation.

## Documentation invariant

`docs/HEMS_Roadmap.md` and this file are authoritative project-state documents. Every HEMS-relevant PR must check both. Any implementation, validation, hardware status, open work, branch/PR, firmware-channel or known-risk change must update both files in the same PR when the documented project state changes. Before merge or promotion to `hems`, both documents must describe the resulting `hems` state accurately. Unmerged or physically unvalidated work must remain explicitly labelled as such.

See [Roadmap](HEMS_Roadmap.md), [control model](HEMS_CONTROL.md), [policies](HEMS_Policies.md), [dispatch](HEMS_Economic_Dispatch.md), [energy data](HEMS_Energy_Data.md) and [native energy/optimizer](HEMS_Native_Energy.md).
