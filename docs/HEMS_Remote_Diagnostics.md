# Outbound Remote Diagnostics

Implemented on `remote-telemetry-sender`, based on **unmerged `redesign-v1` /
[PR #37](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/37)** at
`1018a4c5314941a29ec4c75679274ccd3efeee2f`. This work is not merged into `hems`.
Initial transport validation on 2026-10-30 is recorded below; the rich HEMS
telemetry extension still needs physical validation. The collector and its Traefik/SQLite
infrastructure live separately in `dawid-woitaschek/OpenDTU-HEMS-Telemetry`.

## Setup and API

System > Remote Diagnostics (Ferndiagnose), `/system/remote-diagnostics`, is an
administrator-only feature, independent of HEMS Disabled/Monitor/Active mode.
It defaults disabled with a **60-minute** heartbeat. Configure a public DNS HTTPS
endpoint with an explicit path, for example
`https://collector.example.org/api/v1/telemetry`, and its Bearer token. Ports
1–65535 are supported. Userinfo, IP literals, single-label hosts, query strings,
fragments, percent escapes, whitespace and ambiguous path syntax are rejected.
There is no insecure TLS option, redirect following, custom-header facility,
inbound channel or server-command protocol. Time synchronization is required for
certificate validation. The existing HEMS public roots, hostname verification
and SNI are preserved; Forecast.Solar/Energy-Charts trust behavior is unchanged.

All four endpoints require the existing administrator credentials (even when
public read-only firmware APIs are enabled):

- `GET /api/remote-diagnostics/config`: enabled, endpoint, interval_minutes,
  token_set; never the token.
- `POST /api/remote-diagnostics/config`: existing multipart `data` JSON convention;
  enabled, endpoint, interval_minutes (1–1440), quarter_hour (boolean, default false), optional token. Omit token to keep
  it, set a nonempty token to replace, or disable and set `"token":""` to clear.
  Successful writes return only success. Invalid/failed writes preserve settings.
- `GET /api/remote-diagnostics/status`: operational status, uptime-based attempt /
  success times, delay to next attempt, error category, HTTP result, retry identity,
  spool bounds, acknowledgement, pending and dropped counts. No endpoint secrets.
- `POST /api/remote-diagnostics/send`: schedules the normal worker path; does not
  accept a URL, command, or override of retry bounds. Returns 202 if queued.

Version-1 settings use the repository's bounded versioned JSON settings-store
convention: write temporary file, flush, reread/validate, atomically rename.
Absent, malformed or unsupported files leave the feature disabled. Like existing
firmware credentials, the token is stored in the device's configuration storage;
this is not new at-rest encryption. The generic authenticated file-download API
also blocks the Remote Diagnostics configuration and temporary file. Credentials
are never serialized into telemetry or echoed by configuration/status responses.
Only configuration writes use Flash; uploads and diagnostic queueing do not.

## Runtime and memory bounds

The optional `hems-energy` worker runs telemetry after ordinary energy requests.
No new task is introduced, no self-HTTP request is made, and no provider is polled
for telemetry. Shared system snapshot code preserves the normal status API;
only an explicit API `heap_history=1` request adds an observer checkpoint.
Telemetry has its own RemoteDiagnostics/Upload runtime context and before-request,
before-connect, connected, response, released and idle checkpoints.

The existing RuntimeDiagnostics heap/failure journal remains authoritative.
A separate PSRAM-only POD spool holds **4096 checkpoints** and **64 allocation
failures** (about 277 KiB); records contain context, INTERNAL/PSRAM block/byte
counts and stack watermark. Short synchronized copies preserve ordering; failures
are copied from the original four-record journal in safe scheduler/worker context.
Losses in that staging journal and overwrites in the remote spool are counted.
The allocator callback remains allocation/logging/JSON/network-free.

Payload JSON allocation is capped at 384 KiB of PSRAM and serialized bytes at
**192 KiB**. A batch contains up to 128 oldest pending checkpoints and up to 64
pending failure records. Neither allocator nor output buffer falls back to
INTERNAL. Initial spool allocation occurs for an enabled configuration at boot;
after allocation it remains bounded for the boot lifetime, including while the
sender is disabled. No-PSRAM builds still boot, reporting `psram_required` when
enabled. PSRAM exhaustion or INTERNAL free/largest blocks below 80/40 KiB defer
uploads. These thresholds are conservative starting points, not hardware acceptance.

Each boot gets a random 128-bit identity, unrelated to MAC/hostname/IP/token.
Normal completion advances the per-boot sequence after an accepted=true ACK with
an integer accepted_checkpoint_sequence **exactly matching the staged batch**.
A duplicate=true ACK is valid under the same rule. HTTP success alone is never
sufficient. The serialized body and identity remain immutable across retries;
newer samples cannot be accidentally acknowledged by an earlier duplicate.
Disable/re-enable or credential changes do not replace an unacknowledged body
before its hard 60-minute expiry. Expiry advances the sequence without an ACK.

Retries back off through 1, 2, 4, 8, 16 and at most 32 minutes. Manual requests
respect that floor. Boot, allocation failures and 75% spool occupancy can trigger
an earlier send. Backlogs over one batch drain with at least one minute between
successful uploads; ordinary provider checkpoints do not trigger uploads.
Periodic deadlines remain independent of quarter-hour, plan and manual events.
Ordinary samples older than 60 minutes are pruned separately from acknowledgement.
DNS/connect, handshake, write and read timeouts are bounded; HTTP headers have a
4 KiB total/255-byte line limit and acknowledgements a 1 KiB limit. The request
uses HTTP/1.0 and close, with no persistent TLS session. Chunked/compressed ACKs
are rejected safely. Network/HTTP/ACK/JSON errors never issue HEMS commands or reboot.

A fixed checksummed RTC capsule captures the last completed safe heap checkpoint,
boot identity, diagnostic context, allocation-failure summary and intentional
restart flag. Magic/version/size/checksum and reset reason gate retention. It is
included in the first acknowledged transmission after reboot. This describes the
last completed checkpoint, not an exact crash instruction; power loss can erase
it. There is no Flash crash-capsule write.

## Snapshot scope and validation

System data includes build/source/environment/SDK, uptime/reset diagnostics,
chip/flash/filesystem, radio/connectivity/RSSI and task stack watermarks, without
stable hardware identity or Wi-Fi credentials. Current INTERNAL and PSRAM snapshots
include total/free/minimum/largest/allocated bytes, block counts and fragmentation.
Storage and WARP snapshots reuse the cached provider state: freshness/connection,
available normalized values, counters, faults/alarms, limits and last EVSE command
status. Values are explicitly last observations, accompanied by validity/age.
Raw Modbus data is bounded to 48 address/function/value observations per successful
normal provider cycle. Generic profiles record only semantic registers actually
read; legacy WARP records its actual input/discrete reads. No extra poll or
identity-register read is introduced, and unavailable phase/counter data is not
fabricated. The rich HEMS tree adds normalized tariffs, forecasts and a bounded
future optimizer schedule. No raw household history or arbitrary configuration
snapshot is included.

Collector-compatible summary aliases supplement these detailed fields:

| Summary path | Source and availability |
| --- | --- |
| `heap.internal.free_bytes`, `minimum_free_bytes`, `largest_free_block` | Existing current INTERNAL heap snapshot (unchanged). |
| `heap.psram.free_bytes`, `minimum_free_bytes`, `largest_free_block` | Existing current PSRAM snapshot (unchanged). |
| `storage.state_of_charge` | Numeric `last_observations.soc_pct` only when the storage snapshot and SoC are valid and finite; omitted otherwise. |
| `wallbox.connected` | Boolean cached WARP `reachable`, describing provider connectivity, not vehicle presence. |
| `wallbox.charging` | Boolean cached charging observation only when the snapshot is valid, has an update, and charging is known; omitted otherwise. |
| `diagnostics.allocation_failure_count` | Authoritative current-boot `diagnostics.runtime.heap.failed_alloc_count`, including failures no longer pending in the remote spool. |
| `system.reset_reason` | Existing current CPU-0 `system.resetreason_0` string. |

The detailed `last_observations`, runtime diagnostics and reset/raw fields remain
intact, including last-known measurements when a summary alias is unavailable.
The collector's current `internal/telemetry/telemetry.go` on main was inspected at
`4cba646e45c52b07c9390a45cb6684860c38380c`. This is an additive schema-v1
compatibility layer; ACKs, retry identity/body, spool, TLS and control are unchanged.

Focused native targets: `test_remote_diagnostics`, `test_remote_sender`,
`test_remote_transport`, `test_remote_runtime`; frontend methods/DTO test:
`node test/test_remote_diagnostics_ui.mjs`. Existing storage/EVSE/WARP, runtime
journal, provider-worker and configuration regressions remain applicable.
See both project-state documents for the tests actually run on this branch.

`make -C test test-remote` also checks collector paths/types in a serialized
production sender body with cached hardware test inputs. To compare it against
the actual parser in a local checkout of current collector main, run
`python3 test/test_remote_collector_contract.py --collector /path/to/collector --go-bin /path/to/go`.
This runs the unmodified stdlib-only parser in an isolated temporary Go module,
without the collector server/database or network access. Path/type or extraction
drift fails the test; no physical validation is implied.

## Rich HEMS observation schema (schema version 1)

Every newly constructed payload includes `hems`; a retry retains the original
capture time and body. All values are read from existing cached models, without
polling, replanning, updating learning or submitting commands.

| Path under `hems` | Contents |
| --- | --- |
| `captured_at_utc`, `captured_at_uptime_ms`, `trigger` | Capture Unix seconds, boot uptime milliseconds, bounded trigger array. |
| `global_mode`, `control.storage`, `control.evse` | Actual mode/permissions and manual/automatic readiness masks, named blockers and write possibility. |
| `normalized.grid.power_w`, `normalized.pv.ac_power_w` | AC grid and solar-inverter signals. |
| `normalized.storage` | SoC, AC/DC power, voltage/current, temperature, charging/discharging, fault/alarm, configured/enabled/reachable/update age. |
| `normalized.evse` | Power, vehicle presence, charging, allowed current and device state/age. No invented economic EVSE action. |
| `tariff`, `forecast` | Whitelisted provider/source, freshness/coverage/planning usability, ages, current interval and up to 96 current/future normalized intervals. No settings URLs, plane coordinates or HTTP bodies; URL-like upstream source labels are redacted. |
| `optimizer` | Existing config, plan/energy/configuration revisions, published revision, pending/replan/error/blocker state, validity, runtime, load-learning metadata, costs, terminal value, wear source, hysteresis, bounds and policies. |
| `optimizer.schedule` | Up to 96 current/future cached steps: UTC bounds, market/effective/used price, PV/load/coverage, battery W, stored Wh/SoC, grid import/export Wh and exact Ws, step cost and reason. No new DP calculation. |
| `economic_dispatch` | Unchanged actual `serializeEconomic` state: ownership, plan/flow targets, desired/commanded action, results, failures, export limits and coordination. |
| `shadow_if_active` | Pure desired storage action under explicit Active + granted-storage-permission assumptions. Keeps all other live inputs, pause/dispatch opt-ins, profile mapping, SoC/physical limits, proposed EV constraints, flow guards and bounded retries. Unknown/blocked actions have valid=false, null action/power and a reason. It never updates the dispatcher or optimizer and never submits commands. |
| `constraints`, `flow` | Physical limits, applicable and proposed/actionable EV protection constraints, fresh DPL/topology/headroom observations and existing coordination envelope. Observed grid/PV/storage inputs are in `normalized`. |

Every normalized signal is `{value, quality}`, with `fresh`, `stale` or `unknown`.
Unknown/non-finite values are null; stale known observations retain their value
and explicit stale quality. Genuine fresh zero/false stays distinguishable.
Grid positive means import, negative export; PV positive is AC production.
Storage AC/DC positive means charging, negative discharging. Optimizer/preview
battery W uses that same sign; EVSE power is consumption. Tariff prices use
integer `price_micros_per_kwh` / `import_price_micros_per_kwh`; the optimizer's
`used_price_micros_per_kwh` identifies its configured price basis. A configured
base-load assumption and learned future estimates belong to the plan; no separate
household live-power reading is invented.

Energy arrays report `available_interval_count`, `serialized_interval_count`,
`truncated`. Optimizer reports `schedule_total_steps`, `schedule_serialized_steps`,
`schedule_truncated`; cached stale plans remain available with invalid/stale flags.
HEMS data has priority over checkpoint backlog: remaining diagnostic batch space
is budgeted conservatively under 192 KiB and truncation is explicit. The PSRAM
JSON allocator remains capped at 384 KiB; there is no INTERNAL fallback. Energy
and optimizer records use bounded PSRAM scratch copies (one series or one plan
at a time); large array serialization runs outside their shared model locks.
Scratch exhaustion defers the complete payload instead of publishing partial HEMS
data or consuming INTERNAL heap.

## Triggers and outage semantics

`interval_minutes` is the sole numeric upload interval (default 60). The persisted
`quarter_hour` boolean defaults false, including legacy version-1 files lacking
it. When enabled, observing a change in UTC `time / 900` adds one `quarter_hour`
event; browser timezone has no effect. New published optimizer revisions always
add `plan_revision`. Periodic, quarter-hour, plan, manual and diagnostic events
coalesce into the bounded `trigger` array. Unchanged slots/revisions do not repeat.
Events do not reset the regular heartbeat deadline or bypass minimum spacing and
failure backoff. "Aligned" means the first scheduler/worker opportunity after the
UTC boundary; networking/backoff can defer the actual upload.

Only one PSRAM transmission body exists. New triggers while it is pending update
scalar dirty flags/counters, without storing another full HEMS snapshot. After a
successful ACK the next permitted send captures current data if dirty. The body
is immutable for at most **60 minutes from staging**, not from its last retry.
On expiry the worker frees it, increments local expired/dropped counters and
advances telemetry_sequence **before** a replacement body can be constructed.
It never calls remoteAcknowledge for expiry. An ACK arriving after the deadline
is also discarded locally; its identity is never reused. Existing backoff remains
in force, so a fresh capture happens at the next permitted opportunity.

Diagnostic samples and failed-allocation spool records older than 60 minutes are
pruned in bounded critical-section chunks, with separate expired counters; this
also never changes acknowledgement. Overwrite/drop counters remain separate.
A previous RTC crash capsule is independent of that pruning and is retained for
the first acknowledged post-boot payload under its existing reset lifecycle.
There is no accumulating HEMS queue and no Flash growth during collector outages.
Local expiration counters describe discarded local records, not proof that the
collector failed to commit a transmission whose ACK was lost.

Status adds pending-body age, expired transmissions/dropped telemetry, coalesced
triggers, last capture UTC/uptime/triggers, pending reasons, last published plan
revision observed, quarter-hour enablement and expired spool record counts.
The DE/EN System page exposes these without any secret fields or charts.

## Physical evidence and remaining validation

User-reported field validation on **2026-10-30** used
`generic_esp32s3_usb_quad_psram` on Fusion v2.1/N8R2 hardware. The PSRAM spool was
available. An initial real `tls_failed` transmission remained immutable; after
TLS 1.2 was enabled for this Traefik router, public HTTPS succeeded with HTTP 200.
Checkpoint ACK 24 was accepted, only acknowledged checkpoints were released,
telemetry_sequence advanced and newer checkpoints remained pending. No allocation
failure occurred during that test. Marstek/WARP providers were unreachable:
this establishes no physical provider telemetry, HEMS actuation or long-term
stability validation.

The rich HEMS extension, new triggers/expiry and Active preview have software
validation only until this revision is reflashed and exercised physically.
Measure simultaneous INTERNAL/PSRAM headroom/fragmentation and shared-worker stack
watermark/latency on the 2 MiB PSRAM target, validate real HEMS snapshots/plan
comparisons, quarter-hour coalescing, long outage/lost-ACK expiry, RTC retention and
unattended control/radio behavior. Collector-side graphing remains separate.

## Software validation of the rich extension

Focused software coverage includes legacy v1 configuration/defaults/redaction;
regular/UTC-quarter-hour/published-revision triggers and coalescing; immutable
retries; exact expiry, lost/late ACKs, sequence replacement and freed disabled
bodies; a four-hour collector outage; spool pruning without ACK changes and
independent RTC retention. Real controller/provider/optimizer integration tests
check that Monitor captures and the const preview do not mutate dispatch,
ownership, commands, writes or optimizer revisions; an Active comparison maps to
the same semantic command. Signal quality/null handling, price basis, 96-entry
series/schedule bounds, provider URL/coordinate redaction, full HEMS field/type
contracts and worst bounded payload/JSON limits are covered.

Existing optimizer, economic, coordination/flow, provider, energy/TLS, runtime
and retry regressions passed. Six Remote Diagnostics native targets and the
rich/collector field contracts passed, along with frontend Node/type/lint and
EN/DE 320/1440 px browser checks. Both `generic_esp32s3_usb` and
`generic_esp32s3_usb_quad_psram` built with production web assets. Changed
production C++ passed repository-filtered cpplint and `git diff --check` passed.
No complete firmware matrix or GitHub Actions monitoring was performed. These
local software results are not physical acceptance.
