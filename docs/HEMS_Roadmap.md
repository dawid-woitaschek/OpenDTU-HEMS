# OpenDTU-HEMS development roadmap

Snapshot: 2026-10-15. Integration branch: `integration/hems-next`, live baseline `73a84b639436e712fe5380f071526c30f7c539db` (squash merge of #22).

Current work: `feature/hems-real-control`, one Draft PR directly against integration. No stacked PRs; do not merge this work automatically. Software validation is in progress; physical validation remains open.

## Product model implemented in this package

OpenDTU-HEMS is supervisory home-energy automation, not the primary hardware safety controller. Hardware protections remain authoritative.

- Global Disabled / Monitor / Active, persisted with legacy migration and invalid-value fallback to Monitor.
- Independent Storage and EVSE Monitor only / HEMS control permissions, used by the policy core and providers now.
- Authenticated manual semantic control in Monitor or Active when target readiness allows it.
- Active automatic commands require target write permission; Monitor emits no automatic writes.
- EV protection is the first Active policy. It observes even monitor-only EVSE telemetry and uses stop/release to protect writable storage, with 120 seconds of continuous fresh-false debounce.
- One provider worker/client/mailbox for manual and automatic commands; bounded plans/readback, freshness and generation checks remain.
- Release is attempted once as a courtesy on control/configuration changes. Failure never permanently locks control.
- Durable recovery and temporary sessions are removed. Old journal files are ignored and administratively removable. Identity/replay metadata never authorizes or blocks normal commands.
- Manual UI, useful command/readback/telemetry results, runtime capability diagnostics and profile replacement without reboot.

See [current status](HEMS_Current_Status.md) and [control model](HEMS_CONTROL.md) for implementation facts and flashing steps.

## Next milestones

1. **Flash/reference-hardware validation:** Fusion v2.1 + Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`. Exercise WARP current/stop/release and Marstek acquire/charge/discharge/stop/SoC/release; all four permission combinations; Monitor and Active; communication interruption and return; best-effort permission/configuration release. Record actual physical behavior separately from register readback.
2. **Correct actual WARP/Marstek mapping issues** found on hardware. Verify Marstek V148 power/SoC scaling and uncertain command lifecycle/watchdog. No speculative lifecycle architecture prerequisite.
3. **Dynamic tariff provider:** observe current/future effective prices, validity, timezone/DST and outages.
4. **Economic optimizer Shadow:** observable schedule respecting capacity, power, efficiency, reserve and EV protection, without economic writes.
5. **Optimizer Active:** route validated semantic actions through the same permissions, constraints, worker and planner.
6. **PV forecast / forecast-aware optimization:** optional forecast inputs, validity, explainable influence and useful behavior without forecasts.
7. **Polish, documentation and release.**

Identity/recovery work is optional unless physical testing proves a concrete operational need. There is no architectural mega-milestone before useful operation.

## Verification and resource discipline

Focused native tests and frontend checks run locally. GitHub CI owns the complete firmware matrix and PR firmware-channel publication. Before recommending flashing, verify native tests, frontend build, all HEMS targets including `generic_esp32s3_usb`, exact source/merge-tree correspondence, manifest SHA256/size and normal OTA image. Keep the PR Draft and unmerged.

Baseline #22 `generic_esp32s3_usb` normal OTA image: **2,735,632 bytes** (published artifact). Record this package's exact final artifact size/delta in Current Status after CI. No heavyweight dependencies, PSRAM requirement or TCP/IP/AsyncTCP task-stack changes.

The separate intermittent inbound-new-connection investigation remains unproven as a HEMS issue. It does not block this product model.
