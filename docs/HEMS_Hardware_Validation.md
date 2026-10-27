# HEMS hardware validation runbook

Baseline: current `hems` including merged PR #35 runtime/PSRAM hardening and
refreshed PR #31 CMT2300A work, 2026-10-27. The device/control/dispatch cases below
are **pending physical validation**. Software tests and register readback do not
establish real power flow. PR #35's approximately 21-hour Device A PSRAM run is
accepted for the current runtime-hardening stage; it does not close these control
gates or validate the refreshed CMT behavior. Forecast.Solar has recorded successful
hardware cycles; Energy-Charts still needs a representative live ESP32 run.
PR #34 is separate, unmerged and physically unvalidated; this runbook describes
current `hems`, not its proposed energy-management behavior.

## Preparation and evidence

1. Use the Full-HEMS reference: Device A, Fusion v2.1 / ESP32-S3 / 8 MB flash /
   2 MB quad PSRAM / `generic_esp32s3_usb_quad_psram`, with Shield v1.1 for control
   validation. PSRAM is the expected Full-HEMS memory tier; no-PSRAM is only a
   compatibility target. Record other hardware/network variants separately.
   Before flashing, record the chosen image's source and tested-image commits,
   target, normal OTA size and SHA256 from its published manifest/artifact. Use a
   matching normal OTA `.bin`, never a factory image. A channel name alone is not
   evidence of the tested source. Record available validation results accurately;
   do not wait for or poll GitHub Actions.
2. Log in; save existing settings and device-local modes/limits. Set Monitor,
   dispatch off, and Storage/EVSE Monitor only. Import/replace and bind the exact
   tested repository profiles:
   [`tinkerforge.warp3-smart`](../device-profiles/evse/Tinkerforge-WARP3-Smart.json)
   and [`marstek.venus-e3.v148`](../device-profiles/storage/Marstek-Venus-E-3.0.json).
   Record runtime profile ID, source revision, fingerprint and endpoint/port/unit.
   Repository JSON is not automatically installed on the DTU.
3. Record WARP model/table/firmware and Marstek control/BMS/micro firmware versions;
   V148/community reports are not proof for another version. Enable the WARP Charger
   Modbus table; enable its device-side write access only for control cases. Check
   Marstek's actual device-side remote-control setup. Leave hardware protections
   enabled. Have independent local Stop/reset/disconnection available and supervise
   power-changing tests. Keep other external command writers disabled for attribution.
4. Wait for successful fresh telemetry. Use Devices for semantic commands and
   Advanced Diagnostics for results. Download a redacted support snapshot before
   and after each case; copy transient command results immediately, before another
   command overwrites them. Keep a timestamped local event log for timed cases.
   The export includes the bounded heap journal via `heap_history=1`; capture it
   promptly after failures, before the ring wraps. API observation itself consumes
   resources and is not an atomic timeline. Use consistent observation cadence.

For **every** command, record these three levels independently:

| Level | Evidence |
| --- | --- |
| Request/execution | Target, action/value, command ID/source/generation, stage, write attempted, transport error/Modbus exception and readback verified |
| Fresh HEMS observation | First successful poll after completion, age/quality, normalized SoC/power/charging, permission/readiness and cleanup result |
| Physical device | Timestamped charger/storage display or device UI and independent meter where available; observed direction, value and response delay |

Use `GET /api/hems/status`: `actuation.evse` / `actuation.storage` contain command,
profile, blockers and cleanup; `normalized` contains signal quality;
`shadow.ev_protection` contains charging knowledge and debounce age;
`economic_dispatch` contains runtime economic state. Optimizer status/schedule is
at `GET /api/hems/optimizer?all=1&offset=0` (paginate `has_more`), and provider status
at `GET /api/hems/energy/tariff?all=1` / `forecast?all=1`.
Use the authenticated UI; the existing semantic API contract is in
[HEMS_CONTROL.md](HEMS_CONTROL.md). No raw-register write endpoint or separate
Acquire API is needed. Optional passive wire capture can establish write ordering
and absence of unexpected writes; keep credentials/private addresses out of shared evidence.

Mark a case Pass / Fail / Blocked / Not run. A successful readback with absent or
contradictory physical evidence leaves the physical gate open. Record failures with
snapshots and exact steps before changing mappings or adding lifecycle assumptions.

## WARP3

Run initially in Monitor, dispatch off, Storage Monitor only, EVSE HEMS control.
Choose currents within installation/cable/vehicle limits and the profile's 6–32 A
range; 6 A then 8 A are examples only where supported safely.

| Case | Procedure and acceptance evidence |
| --- | --- |
| W1 Telemetry/detection | Observe unplugged, plugged/waiting, ready and actual charging. Compare raw IEC/charging state, vehicle presence and allowed current with WARP. Charging is true only for documented charging state 3; fault/unusable signals are unknown, not false. Observe faults only if safely available. The Smart profile has no power/session meter telemetry: unknown is correct; never infer actual power from allowed current. |
| W2 SetCurrent | Issue `set_current_a` at two permitted values. Record holding 1002/1003 readback (uint32 mA), fresh allowed-current telemetry and actual charging/current. Other WARP slots, device/cable limits or vehicle demand may lower the effective current; a cap is not a promise of delivered current. |
| W3 Stop | While charging, issue `stop`; verify the Modbus slot is 0 mA, then fresh charging state and actual current/power fall accordingly. Record response time rather than assuming immediate physical response. |
| W4 Release/reacquire | Issue `release`; current profile restores the neutral Modbus-slot cap of 32000 mA, subject to other device limits. Record actual local behavior; Release may allow charging to resume. Repeat SetCurrent → Stop → Release and check that control remains usable. |
| W5 Reconnect | During a low-current session interrupt DTU-to-WARP networking. Record unknown/stale telemetry and rejected new commands; do not treat lost telemetry as stopped charging. Restore the same endpoint, wait for a successful fresh poll and repeat SetCurrent → Stop → Release without acknowledgement/reset of a software latch. Record what WARP did during the outage. |

## Marstek

Run in Monitor, dispatch off, EVSE Monitor only, Storage HEMS control, initially
with no EV charging protection active. Use a mid-range SoC and low power within
both actual device limits and the profile's declared 0–2500 W range. Example
setpoints are 200 W then 400 W when safe. Do not infer physical power from readback.

| Case | Procedure and acceptance evidence |
| --- | --- |
| M1 Telemetry/scale | Compare fresh SoC and AC/DC power, voltage/current, temperature and available energy counters with the actual device at idle, charge and discharge. Current profile: holding 34002 × 0.1 → SoC %, signed holding 30006 × -1 → normalized AC W. Positive AC W means charging, negative discharging. Record raw/profile-decoded/physical values and resolution; diagnose any V148 mismatch before dispatch tests. |
| M2 Acquire | From released control, issue semantic `stop` as the first operation. The session automatically executes profile Acquire (42000 = 21930) before Stop. Record ordered writes/readback and actual remote-control/idle behavior. No standalone public Acquire action exists. |
| M3 Charge | Issue `set_charge_power_w` at two permitted values. Current plan writes 42020 then operating mode 42010 = 1. Verify readback, positive fresh normalized AC power and independently observed charging/SoC trend; note local constraints and response delay. |
| M4 Discharge | Stop first, then issue `set_discharge_power_w` at two permitted values. Current plan writes 42021 then 42010 = 2. Verify negative fresh normalized AC power and real discharge/SoC trend. EV Protection must not currently block this case. |
| M5 Stop | From charge and separately from discharge, issue `stop`. Verify 42010/42020/42021 are zero under the current profile, and observe actual idle/power settling. Record any continued device behavior as a finding. |
| M6 Target SoC | Save the original target; issue `set_target_soc_pct` at a safe whole percentage within 13–100 (42011). Verify readback and device display; where safely practical observe behavior at the target, otherwise leave target enforcement unvalidated. Do not infer a mode change from this setting. Restore the original target and Release. |
| M7 Release/reacquire | From controlled idle and separately low charge/discharge, issue `release` (42000 = 21947). Verify readback and actual device-local mode/power; Release does not promise idle. Start a new Stop/Charge session to verify Acquire works again. |
| M8 Persistence/watchdog | From a known low-power manual session, send no further control writes; keep telemetry only. Record mode/readback/power at 0, 30, 60, 120, 300 and 600 seconds, then longer if needed. Separately repeat with network interruption and a DTU reboot under supervision. Record whether and when external control persists or expires and how it is independently stopped. These are observation intervals, not assumed watchdog deadlines; no validated V148 automatic-release timeout exists. DTU reboot reconstructs no ownership and must not be mistaken for a device Release. |
| M9 Reconnect | Interrupt networking during controlled idle and separately low power. Verify fresh readiness is lost, commands are rejected and failures do not confirm actuation. Restore the same endpoint, wait for fresh telemetry, then explicitly Stop → Release → Charge → Stop → Release. Record physical persistence throughout, including any failed release; recovery must not require a permanent software-latch reset. |

## Permissions, mode and configuration (both devices)

With dispatch off and EV freshly not charging, repeat a low-power/current operation
and Release in Monitor and Active for all four combinations:

| Storage permission | EVSE permission | Expected new manual commands |
| --- | --- | --- |
| Monitor only | Monitor only | Neither target writable |
| HEMS control | Monitor only | Storage only; EVSE telemetry still usable |
| Monitor only | HEMS control | EVSE only |
| HEMS control | HEMS control | Both |

For each writable target: acquire/control, withdraw permission, and capture the
single best-effort old-context Release. Subsequent normal commands must be denied.
Repeat with networking unavailable so courtesy release can fail: record the failure,
then restore networking/permission and fresh telemetry; controls must recover.

While a command/poll is pending, change endpoint (use an isolated test endpoint),
replace the bound same-ID profile with the same tested JSON, and disable/re-enable
the provider in separate runs. Verify old generations/results cannot operate the
new context, old ownership gets one courtesy release, runtime profile reload needs
no reboot, and fresh polling restores readiness. Re-enable/rebind and explicitly
restore permission as needed: disabling clears the binding/control permission.
Never point a test endpoint at an unintended live device.

Leave Active for Monitor and then Disabled in separate owned-session runs: automatic
work must cease, known ownership gets best-effort Release, and Disabled stops
provider polling. Reboot and verify saved mode/permissions; no old ownership is
reconstructed. A failed Release remains a physical device recovery responsibility.

## EV Protection: real WARP3 → real Marstek

Keep economics off. Set Storage HEMS control, EVSE **Monitor only**, Active; WARP
telemetry remains enabled. Start each independent case with known local modes and
fresh telemetry. Use external/local charger control to start/stop real EV charging.

1. With fresh observed Marstek discharge, start actual EV charging. Capture
   `charging_known=true`, charging=true, proposed/effective discharge block and the
   EV Protection Stop's source/result. Observe actual storage discharge stopping.
   A new manual discharge request must be denied; a permitted charge request remains
   usable. Repeat starting from storage charging and idle: neither gets a protection Stop.
2. For the release test, create a **new actual protection Stop**, then leave it
   unsuperseded. Stop EV charging. Record the first fresh false observation as t0
   and follow `condition_age_ms`: no debounce release at 119 seconds; after at least
   120 seconds of continuously fresh false, allow the next controller/worker/poll
   cycle and record semantic Release and actual local storage behavior. Physical
   timing is scheduler/poll bounded, not an assertion of an exact wall-clock instant.
3. Repeat, resuming actual EV charging before 120 seconds: the window resets.
   Repeat again, interrupting WARP telemetry before 120 seconds: unknown/stale cannot
   clear protection; after reconnect, a **new full 120-second fresh-false window**
   is required. Record continuous status/event observations, not just endpoint snapshots.
4. Repeat with no actual protection write (storage initially idle/charging): no
   release obligation is created. In separate actual-Stop runs, send a permitted
   manual Charge or Stop before debounce completes; the later operating command
   supersedes the protection session and must not be released by that old obligation.
   A Target SoC setting alone does not supersede it. Distinguish protection release
   from an independent mode/configuration courtesy release.
5. Repeat the triggering case in Monitor and with Storage Monitor only: proposal
   remains observable, but no automatic Stop/discharge constraint is actionable.
   Record fresh/stale/recovered storage telemetry as well as WARP telemetry.

## Economic Storage Dispatch on real hardware

Run only after WARP/Marstek mapping/lifecycle and protection cases pass. Keep
conservative storage limits, a SoC away from bounds, valid clock, fresh telemetry
and a current covered tariff. Configure capacity, efficiencies and load to the
installation; record reserve/ceiling, grid-charging/economic-discharge permissions,
price basis, target/deadline and whether PV is included. Start dispatch off.

1. **Shadow/default gates:** calculate and inspect current/next rows. Active + Storage
   HEMS control with dispatch off must issue zero economic writes. Repeat with
   dispatch opted in but Monitor, then Active with Storage Monitor only. Planning
   can remain visible; it never authorizes writes by itself.
2. **Current-interval execution:** with all gates satisfied, opt in explicitly.
   Observe a planned positive W → Charge, zero W → Stop/hold, negative W → Discharge
   across actual interval changes. Record UTC interval bounds, plan revision, desired/
   quantized power, economic command source, readback and real power/SoC trend.
   Future rows must not execute early. If live prices do not produce each action,
   keep the case pending or use an isolated, explicitly labelled normalized JSON
   push feed under [the existing energy contract](HEMS_Energy_Data.md), with current
   UTC timestamps and recorded inputs. Real devices still execute the plan; synthetic
   tariff input does not validate the native tariff provider or actual savings.
3. **Idempotence/replanning:** leave inputs stable across several polls; no repeated
   identical operating writes or continuous DP runs. Recalculate and change tariff/PV
   revisions, SoC (including >=1 percentage point), reserve/ceiling and the next interval
   in separate cases. Record revisions/runtime; stale queued writes must be revoked,
   current live bounds respected, and equivalent plans must avoid release/reacquire churn.
4. **Manual precedence:** issue a valid manual storage command. Verify durable
   `manual_pause`, no further economic operating writes through a slot boundary,
   config save, dispatch toggle and DTU reboot. Explicit Resume must clear the pause
   and require fresh planning plus every gate. Invalid/rejected requests and an EVSE
   command are separate cases; they must not silently take over storage economics.
5. **Protection priority/ownership:** during economic discharge, start actual EV
   charging. Verify discharge blocking and physical Stop; exercise the 120-second
   path above. Disable economics while protection owns the Stop: economic cleanup
   must not release protection. Similarly, economic cleanup must not release a
   later manual session. Record the actual source/ownership on each transition.
6. **Authority loss/reconnect:** remove usable current tariff (and PV if used), expire
   the current interval, withdraw permission, leave Active and disconnect storage
   in separate runs. Old plans must not authorize new writes. Cleanup releases only
   economic ownership when telemetry permits; stale/unreachable devices must not
   cause blind Stop/Release storms. Restore inputs/connectivity, wait for fresh
   telemetry and a new current plan, then observe correct execution resuming unless
   manual pause remains set. Gate failure does not guarantee physical idle.
7. **Failure/retry:** where safely reproducible, interrupt communication during a
   command. A failed write/readback never confirms economic actuation. Record fresh
   poll and new plan before retry, at least 30 seconds between attempts, and no more
   than three consecutive failures per UTC 15-minute slot. If the fault cannot be
   safely reproduced, mark hardware retry validation Blocked and retain software
   evidence separately; do not introduce arbitrary register writes to force it.
8. **Combined resource run/finish:** observe normal device polling, UI/MQTT/inverter
   activity, planner/dispatch and ordinary provider refreshes on the PSRAM target.
   Capture runtime, correlated INTERNAL/PSRAM heap checkpoints, allocation failures
   and worker stack watermark before/after and over a separately recorded 24–72-hour
   combined run. This future procedure does not extend the accepted PR #35 run. Require
   no panic/watchdog resets, successful normal refreshes, no unexplained ongoing
   largest-block erosion and >=2 KiB energy-worker free-stack watermark on exercised
   paths. Cached checkpoints do not measure TLS
   peak or prove ownership of an allocation. Disable economics, Stop/Release under
   supervision, restore saved device settings and verify actual final local behavior.

For the remaining Energy-Charts gate, record at least one successful live ESP32
v2 HTTPS fetch, accepted normalized intervals/current coverage, HTTP/DNS/TLS and
heap/stack evidence; retain subsequent normally spaced refresh/recovery evidence.
Forecast.Solar is a regression check here: record configured planes, successful
cycles and retained aggregate, quotas/backoff and resource recovery. Respect existing
provider spacing, quota and Retry-After holds; never accelerate calls to force a
failure. Neither a push feed nor desktop HTTP success closes a native-provider gate.

## Result record

Copy one row per case into the validation report and attach the named evidence:

| Case / UTC time | Firmware + profile + device versions | Mode / permissions / dispatch | Request + transport/readback | Fresh telemetry + physical observation | Result / evidence / follow-up |
| --- | --- | --- | --- | --- | --- |
| Not run | — | — | — | — | Pending |

Include the tested input/config files, response times, network interruptions,
cleanup failures and final physical recovery. Update
[Current Status](HEMS_Current_Status.md) and [Roadmap](HEMS_Roadmap.md) only for gates
actually completed. Economic EV Charging is outside this plan; PR #33 is deferred
design/history, not current product behavior.
