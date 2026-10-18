# OpenDTU-HEMS current status

Snapshot: 2026-10-18. Foundation/productization [PR #23](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/23) merged normally into `integration/hems-next` as `3309804f77d1e882e705955827e89b619cb9bfab`. Policy PR #24 merged as `8348b7cddd91f428c9679b44a670fb698f4a8db6`. Current branch: `feature/hems-economic-dispatch`; runtime dispatch is an explicit default-off opt-in. See [dispatch](HEMS_Economic_Dispatch.md).

## Usable control model

| Global mode | Behavior |
|---|---|
| Disabled | Automatic control off; existing disabled polling semantics preserved |
| Monitor | Telemetry, proposed policy decisions, manual semantic controls for writable targets |
| Active | Monitor features plus automatic policies for writable targets |

Independent **Storage control** and **EVSE control** permissions select **Monitor only** / **HEMS control**. Missing permissions default Monitor only. Both Monitor means observation only even in Active; either mixed configuration is supported. Manual commands do not require Active. Manual storage commands pause opted-in economic automation until explicit Resume; EV Protection retains priority.

Normal readiness requires configured/enabled device/profile, target permission, reachability, fresh valid telemetry, supported bounded operation and no conflicting worker/command. Generation checks, ordered bounded writes and readback remain. Manual and automatic commands share the same mailbox/worker/client. No raw Modbus write API exists.

Durable journals, ownership reconstruction, attestation and temporary-session code/UI/tests are removed. Normal actuation never depends on LittleFS bookkeeping, stable identity or replay-safe release. Old `hems-recovery-*` files remain ignored and can be inspected/deleted using authenticated File Management. Known RAM ownership gets one best-effort old-endpoint release on permission/config changes; failure is diagnostic, then normal operation continues.

## Active EV protection

Fresh EV charging immediately proposes discharge protection. Protection clears only after 120 seconds of continuously fresh not-charging observations; resumed charging cancels debounce, and unknown/stale telemetry cannot clear it.

Active plus Storage HEMS control rejects new discharge commands while allowing charge commands. Semantic Stop is sent only for fresh observed storage discharge; charging and idle storage are left alone. After debounce, release is attempted only after an actual protection intervention that has not been superseded by charging or a later manual operating command. Monitor-only EVSE telemetry remains fully usable. Proposed versus actionable constraints and actual command results are observable. No automatic EVSE policy is installed; its generic Active command path is permission-gated and tested.

## Product UI

Four peer views use the existing OpenDTU visual language: Overview (`/hems`),
Settings (`/hems/settings`), Devices (`/hems/devices`) and Advanced Diagnostics
(`/hems/diagnostics`). Legacy aliases remain supported. Overview shows fresh
energy values, effective tariff prices, interactive tariff/PV charts, provider
readiness and economic Shadow proposals. Settings owns configuration; Devices
owns endpoints, profiles and expandable manual semantic controls. Detailed
provider/control/system diagnostics and paginated optimizer rows remain available.
English, German and French product copy is included. See [UI](HEMS_UI.md).

Tariff cheap/normal/expensive categories are presentation-only. The bounded DP
optimizer uses exact numeric prices, explicit efficiencies, power/SoC bounds,
optional usable PV and terminal reserve. The DP performs no device writes. A separate default-off dispatcher can issue
current-slot semantic storage commands only with every live gate satisfied. Native Energy-Charts DE-LU and Forecast.Solar
providers are included; live provider/TLS validation remains pending.

## Validation and firmware

Focused native coverage exercises the actual controller/policy/providers/mailboxes/executor with deterministic wire/task seams: all four permission combinations, Monitor manual commands, Active-only automatic commands, EV protection and debounce, failure/recovery, release/configuration races and profile capability replacement. Existing parser/catalog/planner/readback tests remain. Frontend type check/build pass. GitHub CI owns the complete matrix and channel publication. Exact tested-head CI, manifest/image verification, firmware size and delta are recorded in [PR #25](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/25).

Reference: Fusion Board v2.1 / Shield v1.1, ESP32-S3, 8 MB flash, `generic_esp32s3_usb`. Policy baseline #24 normal OTA: **2,837,616 bytes**. Dispatch resource deltas and exact tested-head artifacts are recorded in PR #25. No local full firmware matrix, heavyweight dependencies or TCP/IP/AsyncTCP stack changes.

No physical WARP/Marstek control or watchdog/reboot behavior is claimed as tested. Marstek identity absence and replay-safe=false no longer block manual or Active operation. Force-command watchdog reports remain unvalidated for V148.

## After flashing the PR firmware

1. Select the verified `pr-25` firmware channel and `generic_esp32s3_usb`, or upload its normal OTA `.bin` (never the factory image).
2. Log in. In HEMS → Devices / Profiles import the current repository WARP and Marstek JSON. Replace existing same-ID runtime profiles; bind the appropriate profile and configure endpoint/port/unit for each device.
3. In HEMS → Settings choose Monitor. Set each desired device type to HEMS control; leave the other Monitor only if desired. Save each permission.
4. Wait for reachable, fresh telemetry. Exercise WARP current/stop/release and storage bounded charge/discharge/stop/SoC/release. Inspect request, transport, verified readback and fresh observed telemetry separately.
5. For EV protection choose Storage HEMS control, optionally EVSE Monitor only, then Active. Confirm fresh EV charging blocks new discharge requests, stops fresh observed storage discharge, and leaves charging/idle storage alone; fresh-false for 120 seconds permits release only after protection actually intervened. Returning to Monitor stops the automatic producer and attempts release.
6. If a command fails, inspect exact error and telemetry. Once communication/freshness return, controls re-enable automatically. Device-side reset/disconnection remains available independently.

Dynamic tariff and optional PV forecast now have bounded normalized models, authenticated JSON upload, local HTTP polling with one optional shared worker, persisted configuration, freshness/gap/error diagnostics and compact Settings cards. Native Energy-Charts (DE-LU) and Forecast.Solar adapters and pure economic shadow planning are now included; see [native energy and optimizer](HEMS_Native_Energy.md). See [Energy data contracts and API](HEMS_Energy_Data.md). Economic writes require the separate dispatch opt-in and every current gate.

The Admin EVSE false-save error is fixed: the endpoint returns success consistently, the shared HTTP handler is used, and a failed post-save reload produces a warning alongside saved status. Additional native coverage exercises Disabled/Monitor/Active across every device permission combination, stale/unreachable recovery, actual catalog actuator bounds and Marstek byte fixtures, and exact EV Protection debounce boundaries. EVSE telemetry enable/disable transitions now durably clear the old binding and permission before changing enablement, so an API re-enable/reboot cannot revive hidden HEMS control. The runtime no longer polls an implicit WARP mapping when no profile is bound. Disabled storage also resets its persisted control mode to Monitor only, including API payloads that still contain a hidden HEMS permission. Production EVSE/storage settings stores have native persistence/reload coverage.

Completed policies constrain the existing read-only DP. The current dispatch milestone adds opt-in runtime storage actuation, durable manual pause/Resume, plan authority and semantic cleanup. See [precise policy semantics](HEMS_Policies.md). Physical WARP/Marstek/EV Protection validation, live provider/TLS and ESP32 heap/stack/timing validation remain open. Software dispatch is ready for those tests; it does not require an identity/recovery journal. See [Roadmap](HEMS_Roadmap.md).
