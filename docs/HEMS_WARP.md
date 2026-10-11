# Read-only WARP telemetry (PR #3)

Observation only: neither the shared client nor the WARP provider exposes a
Modbus write function. There are no charging controls, policies, DPL inputs,
storage integrations or generic profiles. Real WARP3 validation remains required
before merge; the PR stays draft.

## Setup and configuration compatibility

On the charger enable **Modbus/TCP → Only read access** and select the
**WARP Charger** table (not Bender/Keba). Typical TCP port: 502.
Enable HEMS Monitor and WARP telemetry in Settings → HEMS, then enter a hostname
or IPv4 address and port. No secrets are needed. DNS uses existing lwIP network
configuration. For `.local` names enable mDNS on the DTU, or use IPv4.

Version 1 gains an optional additive object:

```json
{"version":1,"enabled":true,"warp":{"enabled":false,"host":"","port":502}}
```

Missing WARP settings load disabled with port 502, so PR #2/legacy configs work
without migration. Malformed WARP settings load disabled without changing valid
HEMS settings. POST rejects malformed values before mutation; unknown HEMS schemas
remain disabled. The existing save/backup/restore path includes this block.
Legacy PR #2 POST clients omitting WARP preserve current WARP settings.
No telemetry is persisted to flash.

## Official register provenance

Verified on 2026-10-11 against Tinkerforge's official documentation sources:

- [Register table](https://docs.warp-charger.com/docs/interfaces/modbus/registertable/),
  [pinned source a6963560b40c8b6d0cad5035bc2073ec9a55e406](https://github.com/Tinkerforge/warp-charger/blob/a6963560b40c8b6d0cad5035bc2073ec9a55e406/docs.warp-charger.com/docs/interfaces/modbus/registertable.mdx):
  current table version 5, addresses, types, enums and feature requirements.
- [Official decoding example](https://github.com/Tinkerforge/warp-charger/blob/a6963560b40c8b6d0cad5035bc2073ec9a55e406/docs.warp-charger.com/docs/interfaces/modbus/examples.md):
  float32/big-endian decoding.
- [Official API generator](https://github.com/Tinkerforge/warp-charger/blob/a6963560b40c8b6d0cad5035bc2073ec9a55e406/api_doc_generator/modbus_tcp.py):
  typical port 502 and WARP-table selection.
- [Official firmware semantics a114447c4582456d123a0832eaa2d3ce5dd29f88](https://github.com/Tinkerforge/esp32-firmware/blob/a114447c4582456d123a0832eaa2d3ce5dd29f88/software/src/modules/modbus_tcp/modbus_tcp.cpp)
  inspected to resolve documentation omissions: high word first, current in mA,
  session energy as a difference of kWh meter readings, ignored unit ID.
  No Tinkerforge implementation code was copied.

Addresses are zero-based. Each uint32/float32 occupies two 16-bit input registers,
big-endian bytes, high word first.

| Function | Address | Value | Type / interpretation |
|---|---:|---|---|
| 4 | 0 | Register-table version | uint32; accept verified WARP3 versions 2–5 |
| 2 | 0 | EVSE available | Discrete input; required |
| 2 | 1 | Meter available | Discrete input; optional |
| 4 | 1000 | IEC 61851 state | uint32: 0 A, 1 B, 2 C, 3 D, 4 E/F |
| 4 | 1002 | Charging state | uint32: 0 disconnected, 1 waiting, 2 ready, 3 charging, 4 fault |
| 4 | 1010 | Allowed current | uint32 mA → API/UI A by division by 1000 |
| 4 | 2002 | Charging power | IEEE-754 float32 W |
| 4 | 2008 | Current session energy | IEEE-754 float32 kWh |

Read sequence: version (0, two registers), features (0, two bits), EVSE block
(1000, twelve registers), and optional meter block (2002, eight registers).
Intermediate registers are transferred but not exposed. WARP uses fixed unit ID
1, echoed/checked in MBAP; the shared client accepts other unit IDs for later
read-only integrations.

**WARP3 Smart has no meter.** Power/energy stay unknown unless the charger actually
advertises meter availability. Never infer power from allowed current. Negative,
NaN/infinite meter values invalidate the poll. Current must be zero or
6000–32000 mA. Unrecognized enums, missing EVSE and unknown table versions fail
safely. Vehicle presence follows IEC A (absent), B/C/D (present). IEC E/F or charger
fault leaves vehicle/charging booleans unknown while exposing raw states.
Charging is true only for documented charging state 3.

## Transport, concurrency and resources

Selected implementation: a small in-tree read-only Modbus TCP client over the
existing ESP32 lwIP sockets. No new third-party Modbus library/dependency.
PlatformIO already pins `espressif32@6.10.0`, selecting Arduino-ESP32 2.0.17.
New code: GPL-2.0-or-later, matching this project. Existing core:
LGPL-2.1-or-later; lwIP: BSD. Dependency pins are unchanged.

This avoids an additional library task/queue or polling framework. Framing,
transport and WARP decoding are separate. The only constructible requests are
functions 2/4; frames are bounded to 41 bytes/16 registers. No arbitrary function
code/write API, growing buffers or telemetry history exists.

Wi-Fi, EMAC Ethernet and the repository's native W5500 driver share ESP-IDF/lwIP.
W5500 attaches an `esp_netif`, so the same routing and DNS paths apply; it is not
an Arduino Ethernet-library hardware-socket driver. Build support and physical
Ethernet behavior are distinct validation gates.

The cooperative scheduler checks dispatch every 100 ms and never performs
DNS/connect/send/receive. One priority-1 worker owns the client, allocated only
on first enabled use with a 4096-byte internal stack. It sleeps while disabled,
retaining the stack until reboot. No PSRAM is required. Network waits hold no
snapshot/configuration mutex. API handlers copy a small WARP-specific snapshot
under a short mutex. Configuration generations discard old-host/mode results;
an already dispatched read-only poll can drain after disabling, then be discarded.
The scheduler/API never deletes or waits for the worker.

Polls wait at least 3 seconds after completion, including failures, with only one
in flight. DNS wait: 1000 ms; TCP connect: 1500 ms; complete send/fragmented receive
per request: one 1500 ms deadline. Nonblocking sockets close after each poll/error.
Worst-case IPv4 poll: approximately 7500 ms; DNS adds at most 1000 ms.
All waits affect the worker rather than the DPL scheduler.

lwIP DNS submission/completion runs on its TCP/IP thread. The client lives until
reboot so a late callback is safe. A pending lookup is never overwritten or
multiplied after timeout. Once completed, a later poll can consume its result for
the same host or start another lookup for changed configuration.

## API, state and diagnostics

`/api/hems/config` retains administrator-only GET/POST and existing saves.
`/api/hems/status` retains the existing read-only authorization policy and gains
one `warp` object. Settings/info reuse existing cards, forms/footer and English,
German and French translations, explicitly marking WARP read only.

The small WARP-specific snapshot records configured/effective enabled state,
reachability, validity, raw/derived telemetry, last error/Modbus exception,
successful/failed polls and timestamps. It is not a normalized HEMS state store.
Timestamps are device uptime milliseconds, not wall-clock dates.

Failed polls immediately make operational API values JSON null. In-memory last
measurements/timestamp are retained for diagnostics, never substituted with
false/zero. After 15 seconds without success, data becomes stale. Reachability
separately tracks recent protocol responses, including exceptions/invalid data.
Before first success, age/last-success are null. Valid zero/false remain distinct.
Fault booleans and unavailable-meter values are null even on a successful poll.
Recovery automatically clears errors and restores valid data.

UI refreshes every 3 seconds and shows unknown for null values or a failed DTU
status fetch. It displays translated charging states/errors, meter capability,
data age, last success and poll counts. Logs record enable transitions,
availability/recovery and changed errors, not every successful poll.
No frequent flash writes occur.

## Automated tests

`cd test && make test` retains existing tests and adds:

- fixed wire/endian/scaling fixtures; charging/idle/connected/fault/zero;
- meter feature gates, invalid enums/current/NaN/infinity, table version guards;
- failure at every poll step, exceptions, timeout/unreachable, retained samples,
  freshness boundary, recovery, disabled state and uptime rollover;
- production Client.cpp over loopback TCP: fragmented responses, transaction
  mismatch, exceptions, malformed MBAP lengths, truncated replies, deadline;
- DNS platform shims: failure, timeout, late completion, changed host and recovery.

Loopback tests shim Arduino timing/mDNS and lwIP DNS/platform headers only.
Production socket/deadline/framing code is unchanged. They do not validate ESP32
timing, actual W5500 packet transfer or a physical WARP.

`python test/test_hems_config.py .pio/libdeps/generic_esp32s3_usb/ArduinoJson/src`
compiles the production JSON helper bodies with existing ArduinoJson 7.4.3.
It checks legacy/PR #2 configs, backup round trips, unsupported versions and
malformed settings. The USB HEMS CI build runs it after dependency installation.

## Real hardware gate before merge

- [ ] Reference `generic_esp32s3_usb`, 8 MB/no PSRAM: save/restore settings and reboot.
- [ ] Real WARP3 connectivity in read-only WARP-table mode.
- [ ] Unplugged, idle, connected and active charging states.
- [ ] Plausible current and, when available, power/session energy compared with charger.
- [ ] Smart without meter reports unknown power/energy.
- [ ] Age and last-success uptime update correctly.
- [ ] Disconnect: immediate unknown/unreachable, then stale; no fake false/zero.
- [ ] Restore connectivity and verify automatic recovery.
- [ ] Invalid/unroutable/refused host, DNS failure and silent server.
- [ ] Change host/disable Monitor during a pending lookup/poll; reject old results.
- [ ] Hoymiles/inverter, Power Meter and MQTT healthy; DPL responsive throughout.
- [ ] Wi-Fi and W5500 Ethernet behavior where available.
- [ ] Packet capture confirms only function codes 2/4 and no Modbus writes.
- [ ] Free heap/worker stack headroom during prolonged failure/recovery.

None of these physical checks is claimed by mocks or automated tests. Real
heap/stack headroom, router-specific DNS/mDNS behavior, charger firmware/table
behavior and network responsiveness remain installation risks. PR #4 profiles,
storage, policies, Shadow/Active mode and WARP control are out of scope.
