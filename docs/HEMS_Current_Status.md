# OpenDTU-HEMS current status

Snapshot: 2026-10-22. Current development baseline: `hems` at
`617ef19a3af6e64b81a349a867c16facaae6aa04` (merged PR #30).
The architecture, device profiles, semantic control, permission/safety gates,
optimizer, storage policies and Economic Storage Dispatch are substantially
implemented. This unmerged development package adds deterministic HEMS scenarios
and **Economic EV Charging shadow planning only**. Automatic economic EVSE dispatch
is **not implemented**. Physical device validation remains pending.

This feature is stacked on open [PR #32](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/32)
head `59e5b22d`, targeting `feature/hems-validation-readiness`; it is not yet in `hems`.
After #32 merges it can be rebased/retargeted onto `hems`. See the
[EV shadow contract](HEMS_Economic_EV_Charging.md).

## Merged baseline and separate open work

| PR | Result now included in `hems` |
| --- | --- |
| [#23](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/23), [#24](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/24) | Foundation/productization and practical storage/economic planning policies |
| [#25](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/25) | Opt-in Economic Storage Dispatch, manual pause/Resume, ownership/cleanup and browser support snapshot; merged as `82a06f3e` |
| [#27](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/27) | Firmware lookup, verified profile import, Energy-Charts v2, Forecast.Solar contract/plane diagnostics and compatible tooling; merged as `3e61eca0` |
| [#28](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/28) | Promoted the accumulated work through #27 to `hems` as `54a456a6` |
| [#29](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/29) | Runtime memory/TLS hardening, stable optimizer result allocation and bounded heap checkpoints/allocation-failure diagnostics; merged as `fe1cadda` |
| [#30](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/30) | Promoted #29 to `hems` as `617ef19a`; records subsequent ESP32-S3 hardware evidence |

Older PR descriptions contain draft/temporary-channel instructions from their
implementation period. Their merged state and the current `hems` baseline supersede
those instructions. PR #26 was superseded by #27.

[PR #31](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/31) is **open/unmerged**
as of this snapshot. It is the separate `feature/cmt-radio-rework` port of OpenDTU
#3296 and still requires hardware validation. Its commits are not in this baseline.
This scenario/EV-shadow package uses no #31 APIs or implementation and remains valid
whether the radio PR is merged later or never merged.

## Control and planning behavior

| Global mode | Behavior |
| --- | --- |
| Disabled | Automatic control off and provider polling stopped; known ownership gets one best-effort release attempt |
| Monitor | Telemetry, proposed policy decisions and authorized manual semantic commands for ready targets |
| Active | Monitor features plus automatic policies for writable targets |

Storage and EVSE independently select **Monitor only** / **HEMS control**, defaulting
to Monitor only. Both mixed configurations work. Manual commands do not require
Active. Readiness requires a configured/enabled compatible profile, permission,
reachability, fresh valid telemetry, bounded supported input and an available
worker/mailbox. Manual and automatic operations share the semantic actuator,
provider worker/client, bounded plans, generation checks and register readback.
There is no raw Modbus write API.

Known RAM ownership receives one best-effort old-context Release on permission or
endpoint/profile changes. Failure is diagnostic and does not permanently lock
control. Reboot reconstructs no ownership. Old `hems-recovery-*` files are ignored
and can be removed through authenticated File Management; no recovery journal or
identity/replay attestation authorizes normal commands. Device/BMS protections
remain authoritative. Release relinquishes HEMS control and does not guarantee idle.

Active **EV Protection** with Storage HEMS control observes even monitor-only WARP
telemetry. Fresh charging blocks new discharge requests and sends Stop only for
fresh observed storage discharge; charging and idle are left alone. Release needs
120 seconds of continuously fresh not-charging observations and an actual protection
write intervention that has not been superseded. Resumed charging or stale/unknown
EV telemetry resets the fresh-false window. No automatic EVSE producer exists.

The bounded read-only **optimizer** uses exact tariffs, optional PV, fresh SoC,
efficiencies, power/SoC limits and storage policies (grid charging, economic discharge,
one-time target/deadline). Tariff categories only affect presentation. Separate
**Economic Storage Dispatch** is implemented but default-off: calculation enabled,
explicit dispatch opt-in, Active, Storage HEMS control, current-interval authority
and all live constraints/readiness gates must agree before every write. Manual
storage control durably pauses economics until authenticated explicit Resume;
EV Protection retains priority. Ownership-specific cleanup and bounded retry remain.
See [control](HEMS_CONTROL.md), [policies](HEMS_Policies.md) and
[dispatch](HEMS_Economic_Dispatch.md) for precise contracts.

## UI and validation evidence

Overview (`/hems`) presents energy/provider readiness, exact prices, tariff/PV charts
and the storage schedule: current/next action, merged blocks, expected SoC and modeled
benefit. Settings (`/hems/settings`) owns strategy and advanced model configuration;
Devices (`/hems/devices`) owns profiles, endpoints and manual controls. Advanced
Diagnostics (`/hems/diagnostics`) exposes raw control/provider/optimizer details.
English, German and French copy and legacy navigation aliases are included.

Overview now also presents an explicitly shadow-only EV proposal, next change,
charging windows, required/planned energy, completion/shortfall, modeled cost and
storage conflicts. Settings adds remaining-energy/departure/current/price/PV intent;
voltage and phase count stay under Advanced. Diagnostics and the normal support
snapshot expose EV inputs, revisions, coverage and paginated intervals. DST-fold
inputs require an explicit UTC choice. No vehicle SoC or physical delivery is invented.
The staged residual planner preserves the storage DP schedule, excludes storage
discharge intervals and does not claim global joint-home optimality.

The on-demand browser support snapshot (`opendtu-hems-diagnostics/v1`) sequentially
collects existing firmware, system, HEMS, tariff, forecast and paginated optimizer
APIs. It now requests `/api/system/status?heap_history=1` to preserve #29's bounded
heap journal in the same export. Secrets remain redacted; network/location inclusion
is opt-in. No support file is written to LittleFS, and ordinary status polling is
unchanged. Collection errors remain explicit. Sequential snapshots are observations
across time, not an atomic record or proof of physical behavior.

| Area | Software evidence | Physical evidence / remaining gate |
| --- | --- | --- |
| Profiles and semantic control | Deterministic parser/catalog, bounds, readback, mailbox/worker, permissions, config-generation and failure/recovery coverage | WARP3 and Marstek telemetry/actuation/reconnect still require physical validation |
| EV Protection | Production controller/policy/provider tests, permission combinations, exact debounce boundaries and ownership/cleanup | WARP3 → Marstek interaction, including 120-second release, remains unvalidated on devices |
| Deterministic scenarios / EV shadow | Host scenario harness uses fake clocks, scriptable telemetry/connectivity, transport/readback faults and semantic result/cleanup capture; bounded EV planner/service tests cover deadlines, gaps, optional PV, prices, current, partial intervals, revisions and persistence; browser fixtures cover intent save/DST/shortfall presentation | Software evidence only; no physical Modbus-device emulation or automatic economic EVSE control |
| Optimizer/storage dispatch | Planning, current-slot authority, opt-in, durable pause/Resume, ownership, retry and UI tests; merged #25 | Real charge/hold/discharge execution and combined ESP32 timing/resources remain open |
| Forecast.Solar / memory hardening | Contract fixtures, bounded TLS/parser/resource diagnostics and allocation/replan regressions in #27/#29 | #30 records >24 hours on `generic_esp32s3_usb` without PSRAM, successful TLS cycles on both planes and largest INTERNAL block recovery; broader plane/quota/recovery regression coverage remains |
| Energy-Charts v2 DE-LU | Strict v2 parser/fixtures and externally verified HTTP response contract | Successful live ESP32 HTTPS/parser/resource validation remains open; desktop HTTP captures do not close it |

Merged PRs record their historical test/build results. These are software evidence
for those revisions, not new checks run by this documentation update. No leak was
proven by the memory investigation; checkpoint ages and operation context matter.
#29's diagnostics do not turn cached heap samples into failure-instant measurements.

## Next hardware run

Follow the [hardware validation runbook](HEMS_Hardware_Validation.md), starting in
Monitor with dispatch off and both device permissions Monitor only. Reference:
Fusion Board v2.1 / Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`, no PSRAM.
Use the selected tested `hems` firmware (or this PR's own tested image for its snapshot
change); record target, source/image commits, normal OTA size and SHA256 before
flashing. Never substitute an old PR channel or a factory image for that evidence.

The runbook covers WARP3, Marstek sign/scale and lifecycle/watchdog uncertainty,
all permission/configuration transitions, reconnect, EV Protection and Economic
Storage Dispatch. Device outcomes remain pending until recorded with actual device
firmware/profile revisions, command/readback and independent physical observations.
WARP3, Marstek, EV Protection and Economic Storage Dispatch physical validation all
remain pending. No new hardware validation is claimed by this PR. Local checks stay
focused; let
GitHub CI run independently without waiting for, polling or monitoring Actions.
See the [roadmap](HEMS_Roadmap.md) for remaining milestones.
