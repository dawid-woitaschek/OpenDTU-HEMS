# OpenDTU-HEMS current status

Snapshot: 2026-10-15. Live integration baseline: `73a84b639436e712fe5380f071526c30f7c539db`; GitHub confirms #22 merged. Current branch: `feature/hems-real-control`, [Draft PR #23](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/23) against `integration/hems-next`. The old #18/#20/#21 stack is closed and irrelevant.

## Usable control model

| Global mode | Behavior |
|---|---|
| Disabled | Automatic control off; existing disabled polling semantics preserved |
| Monitor | Telemetry, proposed policy decisions, manual semantic controls for writable targets |
| Active | Monitor features plus automatic policies for writable targets |

Independent **Storage control** and **EVSE control** permissions select **Monitor only** / **HEMS control**. Missing permissions default Monitor only. Both Monitor means observation only even in Active; either mixed configuration is supported. Manual commands do not require Active. Active manual commands carry a visible automation-override note.

Normal readiness requires configured/enabled device/profile, target permission, reachability, fresh valid telemetry, supported bounded operation and no conflicting worker/command. Generation checks, ordered bounded writes and readback remain. Manual and automatic commands share the same mailbox/worker/client. No raw Modbus write API exists.

Durable journals, ownership reconstruction, attestation and temporary-session code/UI/tests are removed. Normal actuation never depends on LittleFS bookkeeping, stable identity or replay-safe release. Old `hems-recovery-*` files remain ignored and can be inspected/deleted using authenticated File Management. Known RAM ownership gets one best-effort old-endpoint release on permission/config changes; failure is diagnostic, then normal operation continues.

## Active EV protection

Fresh EV charging immediately proposes discharge protection. Protection clears only after 120 seconds of continuously fresh not-charging observations; resumed charging cancels debounce, and unknown/stale telemetry cannot clear it.

Active plus Storage HEMS control rejects new discharge commands while allowing charge commands. Semantic Stop is sent only for fresh observed storage discharge; charging and idle storage are left alone. After debounce, release is attempted only after an actual protection intervention that has not been superseded by charging or a later manual operating command. Monitor-only EVSE telemetry remains fully usable. Proposed versus actionable constraints and actual command results are observable. No automatic EVSE/economic policy is installed yet; its generic Active command path is permission-gated and tested.

## Visible UI

HEMS → Settings (`/hems/settings`) includes global modes, prominent independent device write permissions, Storage and EVSE status, current constraints, manual semantic forms, results and advanced profile/worker diagnostics.

- WARP: current from profile bounds (reference 6..32 A), stop, release.
- Storage: charge/discharge power, stop, target SoC, release from profile metadata.
- Results: request/value/source, state, transport/exception, register verification, completion uptime/age and physical telemetry freshness. Register echo is explicitly distinguished from physical behavior.
- Unavailability reasons explain configuration, permission, profile version/metadata, reachability, freshness or busy state.
- Device/profile administration previews capability changes and replaces same-ID imports. Bound profiles reload safely without reboot. Catalog profiles are never automatically installed.

## Validation and firmware

Focused native coverage exercises the actual controller/policy/providers/mailboxes/executor with deterministic wire/task seams: all four permission combinations, Monitor manual commands, Active-only automatic commands, EV protection and debounce, failure/recovery, release/configuration races and profile capability replacement. Existing parser/catalog/planner/readback tests remain. Frontend type check/build pass. GitHub CI owns the complete matrix and channel publication. Exact tested-head CI, manifest/image verification, firmware size and delta are recorded in [Draft PR #23](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/23).

Reference: Fusion Board v2.1 / Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`. Before: #22 normal OTA **2,735,632 bytes**. After/delta: see the exact final artifact measurement in Draft PR #23. No local full firmware matrix, heavyweight dependencies or TCP/IP/AsyncTCP stack changes.

No physical WARP/Marstek control or watchdog/reboot behavior is claimed as tested. Marstek identity absence and replay-safe=false no longer block manual or Active operation. Force-command watchdog reports remain unvalidated for V148.

## After flashing the PR firmware

1. Select the verified `pr-23` firmware channel and `generic_esp32s3_usb`, or upload its normal OTA `.bin` (never the factory image).
2. Log in. In HEMS → Devices / Profiles import the current repository WARP and Marstek JSON. Replace existing same-ID runtime profiles; bind the appropriate profile and configure endpoint/port/unit for each device.
3. In HEMS → Settings choose Monitor. Set each desired device type to HEMS control; leave the other Monitor only if desired. Save each permission.
4. Wait for reachable, fresh telemetry. Exercise WARP current/stop/release and storage bounded charge/discharge/stop/SoC/release. Inspect request, transport, verified readback and fresh observed telemetry separately.
5. For EV protection choose Storage HEMS control, optionally EVSE Monitor only, then Active. Confirm fresh EV charging blocks new discharge requests, stops fresh observed storage discharge, and leaves charging/idle storage alone; fresh-false for 120 seconds permits release only after protection actually intervened. Returning to Monitor stops the automatic producer and attempts release.
6. If a command fails, inspect exact error and telemetry. Once communication/freshness return, controls re-enable automatically. Device-side reset/disconnection remains available independently.

Dynamic tariff and optional PV forecast now have bounded normalized models, authenticated JSON upload, local HTTP polling with one optional shared worker, persisted configuration, freshness/gap/error diagnostics and compact Settings cards. No commercial service was selected. See [Energy data contracts and API](HEMS_Energy_Data.md). No economic writes are generated.

The Admin EVSE false-save error is fixed: the endpoint returns success consistently, the shared HTTP handler is used, and a failed post-save reload produces a warning alongside saved status. Additional native coverage exercises Disabled/Monitor/Active across every device permission combination, stale/unreachable recovery, actual catalog actuator bounds and Marstek byte fixtures, and exact EV Protection debounce boundaries. EVSE telemetry enable/disable transitions now durably clear the old binding and permission before changing enablement, so an API re-enable/reboot cannot revive hidden HEMS control. The runtime no longer polls an implicit WARP mapping when no profile is bound. Disabled storage also resets its persisted control mode to Monitor only, including API payloads that still contain a hidden HEMS permission. Production EVSE/storage settings stores have native persistence/reload coverage.

Next work: real WARP/Marstek/EV Protection validation, provider-specific adapters when selected, explicit economic strategy decisions and Shadow optimization, then Active optimization and release polish. See [Roadmap](HEMS_Roadmap.md).
