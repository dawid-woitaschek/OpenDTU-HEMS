# Outbound Remote Diagnostics

Implemented on `remote-telemetry-sender`, based on **unmerged `redesign-v1` /
[PR #37](https://github.com/dawid-woitaschek/OpenDTU-HEMS/pull/37)** at
`1018a4c5314941a29ec4c75679274ccd3efeee2f`. This work is not merged into `hems`.
Physical field validation remains pending. The collector and its Traefik/SQLite
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
  enabled, endpoint, interval_minutes (1–1440), optional token. Omit token to keep
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
The per-boot telemetry sequence advances only after an accepted=true ACK with
an integer accepted_checkpoint_sequence **exactly matching the staged batch**.
A duplicate=true ACK is valid under the same rule. HTTP success alone is never
sufficient. The serialized body and identity remain immutable across retries;
newer samples cannot be accidentally acknowledged by an earlier duplicate.
Disable/re-enable or credential changes do not replace an unacknowledged body.

Retries back off through 1, 2, 4, 8, 16 and at most 32 minutes. Manual requests
respect that floor. Boot, allocation failures and 75% spool occupancy can trigger
an earlier send. Backlogs over one batch drain with at least one minute between
successful uploads; ordinary provider checkpoints do not trigger uploads.
Periodic uploads resume the configured interval when the backlog is small.
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
fabricated. No tariff, household history, schedule or arbitrary configuration
snapshot is included.

Focused native targets: `test_remote_diagnostics`, `test_remote_sender`,
`test_remote_transport`, `test_remote_runtime`; frontend methods/DTO test:
`node test/test_remote_diagnostics_ui.mjs`. Existing storage/EVSE/WARP, runtime
journal, provider-worker and configuration regressions remain applicable.
See both project-state documents for the tests actually run on this branch.

Physical validation must measure peak simultaneous INTERNAL/PSRAM use, allocator
pressure, the shared worker's stack watermark and provider latency; verify actual
public-CA TLS/SNI through Traefik, NTP/DNS/offline/timeout recovery, ACK-loss retries,
spool overflow, RTC retention after real panic/watchdog/reset, and long-term HEMS,
radio, WARP and Marstek behavior. Software checks do not establish those results.
