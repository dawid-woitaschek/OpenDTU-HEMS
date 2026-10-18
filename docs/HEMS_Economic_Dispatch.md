# Economic storage dispatch

Software milestone after policy PR #24 (normal integration merge
`8348b7cddd91f428c9679b44a670fb698f4a8db6`). Hardware-free validation does not
establish physical battery behavior. The new dispatch PR remains Draft.

## Authorization and opt-in

`config.dispatch_enabled` is an additive boolean in the existing version-1
`/hems-optimizer.json`. Missing fields default **false**, including old clients
saving an old config. Active + Storage HEMS Control alone never opts in.
`config.enabled` still controls Shadow calculation independently. Dispatch needs
both, Active, Storage HEMS Control, an enabled/configured provider, compatible
writable profile, reachable device, fresh usable SoC/telemetry, no active device
fault, current effective constraints, a valid current plan and semantic readiness.
Every bounded write rechecks authorization through the existing provider callback,
generation gate and semantic executor. A calculated plan is not authorization.

Only storage is dispatched. There is no economic EVSE producer, raw Modbus path,
register API, second actuator, new protocol, worker or dependency. Price categories
remain display-only; exact numeric prices drive the existing DP unchanged.

## Manual priority and durable pause

Priority is hardware/BMS → EV Protection/current actionable constraints →
authorized manual semantic control → economic automation. Before accepting a
ready, supported, constraint-permitted manual storage command while dispatch is
enabled, the controller persists `manual_pause: true` in the same optimizer file.
Failure to persist rejects the manual request with no device write. Invalid or
unavailable requests do not pause automation. An accepted pause remains if a later
queue/readiness race rejects the command; retrying the manual command is safe.
Pending/running work is never overwritten.

The pause applies to charge, discharge, target SoC, Stop and Release, in Monitor
as well as Active. Manual EVSE commands do not pause storage economics. Saving
configuration, disabling/re-enabling dispatch, slot changes and reboot never
clear the pause. Only authenticated `POST /api/hems/optimizer/resume` clears it,
atomically, and revokes the previous plan before requesting fresh calculation.
Resume requires calculation and dispatch enabled; Active and device permission
are still checked separately before actuation. No timer silently resumes.

There are no per-cycle flash writes. Only settings changes and actual manual
pause/resume transitions persist. No ownership or plans are persisted. A malformed
present persisted pause fails closed. Legacy files without it remain compatible.

## Plan authority and bounded replanning

The read-only optimizer service publishes a small copy of its **first/current
half-open UTC interval**, carrying a plan revision. Future rows never enter a
mailbox. The command carries that revision; the provider callback compares it to
the current authoritative view before every write. Provider generation independently
pins endpoint/profile and permission. Global mode is part of the control revision.

Authority is lost immediately on optimizer/policy/dispatch configuration changes,
manual pause/resume, control mode/provider generation changes, energy-data revision,
tariff freshness/usability changes, required PV freshness/usability changes,
storage readiness/fault/SoC quality changes, at least **one percentage point** of
SoC drift from the planning input, effective/proposed constraint changes, clock
rollback or leaving the exact first plan interval. Smaller SoC changes do not
rerun DP; live reserve/ceiling checks still stop power at the configured boundary.
Any tariff or forecast revision conservatively invalidates the plan even if PV
was excluded. PV is required only when the plan included it.

The shared energy worker calculates on those changes, explicit recalculation,
resume, failed economic command, and the next first-interval boundary (including
a split target deadline, tariff boundary or horizon end). A **five-second minimum**
between runs bounds both explicit and automatic requests; changes coalesce while
waiting. Unchanged polling does not rerun DP. No unconditional per-minute DP loop
remains. Plans with unavailable inputs await meaningful input changes or the next
15-minute boundary. Runtime duration remains exposed by the existing optimizer.

The controller evaluates dispatch once per second on its existing scheduler;
per-write checks are immediate. No new task or full-plan copy is added. A new
interval requires a fresh plan before dispatch. Partial future coverage can still
authorize its exact covered current interval; target-horizon limitations remain
visible under the existing policy semantics.

## Power, idle and release

Both the planner and normalized storage model use **positive charging, negative
discharging**. Positive current `batteryW` maps to `set_charge_power_w(abs(W))`;
negative maps to `set_discharge_power_w(abs(W))`. Power is bounded by configured
limits, current constraints and semantic profile min/max/step. Quantization rounds
down on the profile's declared input lattice; sub-minimum requests hold with Stop
instead of rounding upward. No device-specific step is invented.

Zero uses semantic **Stop**, a deliberate economic idle/hold with ownership. It
never automatically uses Release. A profile without Stop cannot implement that
plan interval; dispatch reports the unsupported action and relinquishes only its
own old control. Release means economic control no longer applies, and may allow
the device's local logic to resume. It is not a claim that the battery stays idle.

Confirmed action and quantized power suppress identical writes across polls and
equivalent replans. Action/power changes or lost ownership can produce one new
bounded command. There is no monetary hysteresis.

## Ownership, failure and cleanup

Commands distinguish manual, EV Protection and economic origins. The storage
provider tracks the source of its current RAM session; accepted manual control
takes responsibility for that session immediately. Economic cleanup checks that
source, and never releases manual or EV Protection ownership. Charging may
supersede a protection Stop when all charge constraints allow it; the protection
release obligation then ends. Economic idle/discharge cannot supersede a retained
protection intervention. Existing EV Protection fresh-false debounce and actual
intervention release semantics remain.

On lost authority, pending economic work is cancelled and its revision revoked;
running work is revoked at the next bounded write. Completion cannot overwrite a
newer manual command because the mailbox and worker serialize both, and the pause
and source gates remain authoritative.

An obsolete economic charge/discharge/hold gets a best-effort semantic Release
only while economics still owns it and device telemetry is reachable/fresh. Lost
SoC can invalidate planning without preventing release if provider telemetry is
otherwise usable. Unreachable/stale devices wait; no blind repeated Stop/Release
is sent. Endpoint/profile/permission changes and leaving Active retain the existing
one-shot old-context courtesy release, including the explicitly allowed release
after permission withdrawal. The shared provider exposes pending, success and
failure separately. Cleanup failure is diagnostic, not a permanent lockout or a
claim that local mode was restored.

Transport errors, Modbus exceptions, readback mismatch and revoked writes never
confirm economic actuation. A failure invalidates the plan and confirmed action.
Retry needs a fresh valid revision, a subsequent successful telemetry poll,
readiness, and at least **30 seconds** since the prior attempt. At most **three
consecutive failed attempts per UTC 15-minute slot** are allowed. A successful
command resets that retry count. The next slot can retry
after fresh planning; there is no permanent latch. Existing provider failure
handling closes the connection and requests fresh polling.

Reboot reconstructs no ownership and starts with no calculated plan. Fresh
telemetry, fresh calculation, opt-in and every authorization gate are required.
A persisted manual pause still prevents economic actuation. No recovery journal,
identity attestation, watchdog assumption or replay prerequisite is introduced.

## API, UI and diagnostics

Existing authenticated optimizer config/recalculate and semantic command routes
remain. Resume is authenticated too. Atomic temp/write/flush/verify/rename and
the 2048-byte settings/request bound remain. Settings expose the explicit dispatch
checkbox. Overview, Settings and Diagnostics distinguish Shadow planning,
dispatch opt-in, global mode, Storage permission, paused/blocked/waiting/operating
states, and cleanup pending/failed. Devices explains manual precedence and links
to explicit Resume. English/German/French parity and responsive layouts remain.

Optimizer GET adds manual pause, config/plan/energy revisions and replan reason.
Its `dispatch` object and status `economic_dispatch` expose the current slot,
desired/last action and power, command/result, source/owner, provider generation,
confirmed state, failure count and cleanup state. The planner's own `shadow_only`
and `device_writes: false` fields still describe its read-only calculation; the
separate dispatch object describes runtime control. Detailed JSON remains in
Advanced Diagnostics.

## Remaining physical validation

- Marstek V148 acquire/charge/discharge/Stop/Release lifecycle, actual sign/scale,
  command persistence and watchdog behavior.
- WARP lifecycle where it affects protection and EV Protection end to end.
- ESP32-S3 planner/dispatcher runtime, heap and task-stack headroom without PSRAM.
- Live energy providers, freshness, connectivity recovery and TLS behavior.

Software tests and register readback do not prove any of these physical behaviors.
