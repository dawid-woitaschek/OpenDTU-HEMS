# OpenDTU-HEMS development roadmap

Snapshot: 2026-10-18. Integration branch: `integration/hems-next`, foundation merge `3309804f77d1e882e705955827e89b619cb9bfab` (normal merge of #23).

Foundation/productization PR #23 is merged into integration with its original history.
Its final source head `141f00857581d5664183f3025f9f133bdf2fe971` passed all eight
HEMS targets, six inherited targets, host/config tests and lint/format checks.
Physical validation remains open and is not claimed by milestone completion.

Policy PR #24 merged normally as `8348b7cddd91f428c9679b44a670fb698f4a8db6`.
Current work: `feature/hems-economic-dispatch`, explicitly opted-in runtime storage
control through the existing semantic actuator. See [dispatch](HEMS_Economic_Dispatch.md). See [policy contract](HEMS_Policies.md).

## Product model implemented in this package

OpenDTU-HEMS is supervisory home-energy automation, not the primary hardware safety controller. Hardware protections remain authoritative.

- Global Disabled / Monitor / Active, persisted with legacy migration and invalid-value fallback to Monitor.
- Independent Storage and EVSE Monitor only / HEMS control permissions, used by the policy core and providers now.
- Authenticated manual semantic control in Monitor or Active when target readiness allows it.
- Active automatic commands require target write permission; Monitor emits no automatic writes.
- EV protection is the first Active policy. It observes even monitor-only EVSE telemetry and blocks new discharge commands and stops only fresh observed storage discharge, preserving charging and idle modes. Release follows 120 seconds of continuous fresh-false only after an actual intervention.
- One provider worker/client/mailbox for manual and automatic commands; bounded plans/readback, freshness and generation checks remain.
- Release is attempted once as a courtesy on control/configuration changes. Failure never permanently locks control.
- Durable recovery and temporary sessions are removed. Old journal files are ignored and administratively removable. Identity/replay metadata never authorizes or blocks normal commands.
- Manual UI, useful command/readback/telemetry results, runtime capability diagnostics and profile replacement without reboot.
- Advanced Diagnostics can create a privacy-redacted `opendtu-hems-diagnostics/v1` support snapshot entirely in the browser. Existing firmware/system/HEMS/energy/optimizer APIs are collected sequentially; no snapshot file is written to LittleFS.

See [current status](HEMS_Current_Status.md) and [control model](HEMS_CONTROL.md) for implementation facts and flashing steps.

## Next milestones

1. **Flash/reference-hardware validation:** Fusion v2.1 + Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`. Exercise WARP current/stop/release and Marstek acquire/charge/discharge/stop/SoC/release; all four permission combinations; Monitor and Active; communication interruption and return; best-effort permission/configuration release. Record actual physical behavior separately from register readback. Use the browser-only diagnostic snapshot before/after representative runs to preserve firmware/system/provider/optimizer/dispatch evidence without device-side logging files.
2. **Correct actual WARP/Marstek mapping issues** found on hardware. Verify Marstek V148 power/SoC scaling and uncertain command lifecycle/watchdog. No speculative lifecycle architecture prerequisite.
3. **Dynamic tariff provider:** normalized domain, JSON push/local HTTP adapters, status/API/UI and deterministic tests implemented; native Energy-Charts DE-LU and effective pricing are implemented; live-provider/on-device TLS validation remains. See [Energy data](HEMS_Energy_Data.md).
4. **Economic optimizer Shadow:** bounded cost-minimizing DP, explicit assumptions, normalized tariff/PV, fresh SoC, EV constraints, API/UI and software tests implemented. Validate on-device resources and real inputs; see [native energy and optimizer](HEMS_Native_Energy.md).
5. **Practical user/economic policies, Shadow-only (software implemented and validated in PR #24):** grid-charging and economic-discharge permissions, existing reserve/upper planning bounds and a one-time target/deadline constrain the DP. Exact prices drive decisions; tariff categories remain presentation-only. Minimum-saving hysteresis and recurring schedules are deferred. See [policies](HEMS_Policies.md).
6. **Economic storage dispatch (software implemented and validated in Draft PR #25; physical gates open):** explicit default-off opt-in, current-slot plan authority, durable manual pause/Resume, source ownership, bounded replanning/retry and semantic cleanup. Physical validation remains open. See [dispatch](HEMS_Economic_Dispatch.md).
7. **PV forecast / forecast-aware optimization:** optional normalized forecast domain, JSON push/local HTTP, W/Wh aggregation and API/UI implemented alongside tariff. Forecast.Solar with six independent planes and PV-aware shadow planning are implemented; live full-UTC response/quota validation remains. HEMS works without forecasts.
8. **Product UX / planning presentation:** make the generated storage schedule a first-class daily-use view: current action, next actions, charge/hold/discharge blocks, expected SoC progression and truthful reasons. Simplify normal policy controls into a storage-strategy surface while keeping implementation detail in Advanced Diagnostics. Do not make cheap/normal/expensive presentation categories into control rules.
9. **Deferred policy expansion:** minimum-economic-benefit/hysteresis and recurring target/reserve schedules, after real dispatch behavior is validated. Avoid a generic rule-engine DSL.
10. **Economic EV charging:** define product semantics first, then add an EVSE optimizer/dispatch policy through the existing semantic actuator and permission model. No automatic economic EVSE producer exists yet.
11. **Polish, documentation and release.**

Identity/recovery work is optional unless physical testing proves a concrete operational need. There is no architectural mega-milestone before useful operation.

## Verification and resource discipline

Focused native tests and frontend checks run locally. GitHub CI owns the complete firmware matrix and PR firmware-channel publication. Before recommending flashing, verify native tests, frontend build, all HEMS targets including `generic_esp32s3_usb`, exact source/merge-tree correspondence, manifest SHA256/size and normal OTA image. Merge completed software milestones into integration with their validation limits explicit; economic dispatch requires explicit opt-in and its separate software/physical validation limits remain visible.

The support snapshot is a support/validation transport, not a logging subsystem: it is generated only on demand, secrets are always redacted, network/location data are opt-in, and no persistent diagnostic history is created on the device.

Baseline #22 `generic_esp32s3_usb` normal OTA image: **2,735,632 bytes** (published artifact). The exact tested-head final artifact size/delta is maintained in PR #23, linked from Current Status. No heavyweight dependencies, PSRAM requirement or TCP/IP/AsyncTCP task-stack changes.

The separate intermittent inbound-new-connection investigation remains unproven as a HEMS issue. It does not block this product model.
