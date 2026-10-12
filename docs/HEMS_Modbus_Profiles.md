# Modbus profiles (PR #4)

Profiles describe reads, never execute control. Import/validation does not select
or activate a device. There is no write frame constructor, control endpoint,
external-control permission, generic provider, persistent polling, Shadow/Active
mode or Marstek integration in this milestone. WARP remains its existing provider
with FC2/FC4 requests and unchanged failure/null behavior.

## v1 definition

The exact identifier is `opendtu-hems-modbus-profile/v1`. The
[synthetic example](examples/modbus-profile.json) is test data, not a register map
for any manufacturer. The firmware ships it as built-in `example.synthetic`.
Use the [import fixture](examples/import-profile.json), with a different stable id,
for reference import tests; importing the built-in id intentionally fails.

All top-level fields below are required. Unknown fields at every level are errors.
JSON strings must be printable ASCII, without backslash escapes. This deliberate
v1 restriction also rejects escaped keys, embedded NUL, malformed UTF-8 and
ambiguous duplicate key spellings. International metadata and URLs are deferred;
the UI itself has English, German and French translations.

- `schema`: exact v1 identifier.
- `id`: 1–48 characters, starting with `[a-z]`, followed by `[a-z0-9._-]`.
- `metadata`: required nonempty `vendor`, `model`, `source`, `source_revision`;
  optional nonempty `description`. Each is at most 96 ASCII bytes.
- `provenance`: required nonempty `author`, `license`, each at most 96 bytes.
  These are declarative attribution, not a firmware license grant or trust claim.
- `transport`: exact `protocol: "modbus_tcp"`, `default_port: 1..65535`,
  `unit_id: 1..247`, `address_base: 0|1`. Address base is subtracted to produce
  zero-based wire addresses; a one-based profile cannot contain address zero.
- `telemetry`: array of 1–24 entries with unique semantic keys.

Telemetry fields:

| Field | Meaning |
|---|---|
| `key` | Required, same alphabet as id, maximum 32 bytes; semantic label only |
| `space` | Required: `discrete` (FC2), `holding` (FC3), `input` (FC4) |
| `address` | Required integer; after address-base conversion, 0..65535 |
| `count` | Required integer; exactly 1 for bool/16-bit, exactly 2 for 32-bit |
| `type` | Required: `bool`, `uint16`, `int16`, `uint32`, `int32`, `float32` |
| `byte_order` | Optional: `big` (default) or `little`, within each 16-bit word |
| `word_order` | Optional: `high_first` (default) or `low_first`, for two-word values |
| `scale`, `offset` | Optional numeric settings; defaults 1 and 0 |
| `unit` | Optional nonempty descriptive string, maximum 16 ASCII bytes |
| `min`, `max` | Optional inclusive range applied after scale/offset |
| `invalid_raw` | Optional unsigned raw-bit sentinel, before scale/offset |

Bool is permitted only in discrete space, with one bit in one payload byte. Bool
has no scaling/range/sentinel or reordered bytes. Single-register values reject
low-word-first ordering. All settings must be finite with absolute magnitude at
most 1e12; scale must be nonzero. Negative scale is useful for power sign
conversion. Sentinel is at most 65535 for a 16-bit entry, otherwise uint32.
Address plus count must not exceed the 65536-address wire space.

Numeric results use double precision. Settings are normalized through the existing
ArduinoJson canonical decimal representation before conversion to double. This
avoids promoting compact float parsing noise into range failures at decimal
scaling boundaries (e.g. 1000 * 0.1 at a maximum of 100). Canonical export retains
ArduinoJson numeric serialization precision. float32 uses
IEEE-754 bits, and NaN/infinity are invalid. Decode requires exact payload length;
range violations, sentinels and malformed payloads produce `valid:false,value:null`.
Valid numeric zero and boolean false remain valid. Transport errors cannot be
interpreted as sample bytes. Sample diagnostics never contact a host.

There are no expressions, scripts, formulas, bitfield programs, enum transforms,
byte arrays, 64-bit counters or multi-register strings. A provider can later combine
separate scalar telemetry keys into normalized state. `control`/`failsafe` and
arbitrary function-code fields fail validation even when empty. Freezing write
semantics is deferred to PR #8 rather than storing executable-looking metadata.

## Resource and input limits

| Resource | Compile-time bound |
|---|---:|
| Input and normalized export JSON | 6144 bytes each |
| Imported profiles | 4 |
| Telemetry entries | 24 (normalized export must still fit) |
| Registers per profile entry | 1 or 2 |
| Shared read request | 16 registers/bits; unchanged 41-byte response frame |
| JSON nesting | 4, checked before allocation and by ArduinoJson |
| Object members | 16, checked by lexical gate |
| JSON allocator payload memory | 12288 bytes per document |
| Profile model | static assertion <=4096 bytes; measured native/ESP32 3232 |
| Sample decode bytes | 4 |
| Import requests buffered concurrently | 1 |
| Profile file payloads | <=24576 bytes committed + <=6144 temporary |
| Filesystem free-space reserve when committing | 8192 bytes |

Parser allocations are bounded, with no user-sized vectors or string growth.
The lexical scan rejects oversize, duplicate object keys, trailing JSON, invalid
number syntax, excess nesting/array complexity and unsafe strings first. Schema
validation rejects missing fields, wrong scalar types, invalid port/unit/base,
unknown schema, duplicate telemetry keys, count/type mismatch, address overflow,
nonfinite settings and unknown metadata.

Profiles are allocated only during explicit API operations; no RAM registry is
held at boot, and no PSRAM is assumed. Parsing uses a second candidate model so
failure never mutates the caller's good model. Import holds candidate/stored
models; parse temporarily holds another model. A conservative application payload
budget is <=12 KiB models + 6 KiB request + 6 KiB stored-file read + 12 KiB JSON
allocator = 36 KiB during a worst-case import scan, plus allocator headers, stack
and the existing HTTP/network machinery. Typical small profiles use less.
A native 24-entry fixture normalizes to 3604 bytes and its normalization allocator
peaks at 8470 bytes. ESP32 DWARF confirms the model is also 3232 bytes. The largest allowed input buffer is 6144 bytes. The normalized
synthetic example is 561 bytes of LittleFS payload; filesystem
block/metadata costs are additional. This is temporary heap, not persistent/static RAM.

## Persistence, identity and corruption

The existing LittleFS partition is 192 KiB. Profiles are separate from
`/config.json`; no HEMS config schema change is made. Four fixed internal slot paths
are chosen by firmware; input filenames and ids never form filesystem paths.
No public API accepts a filesystem path. The built-in id is reserved and cannot
be replaced or deleted. Imported ids must be unique. Replacement requires the
`replace` query argument matching the incoming JSON id and an existing imported
profile. Import without it rejects duplicate ids. Filename is not identity.

Validation and normalized-size checks precede any flash mutation. Commit writes a
single fixed temporary file, checks the complete write, flushes/closes it, verifies contents using a fixed
128-byte buffer, then
uses LittleFS atomic rename to replace the slot. It never removes the destination
first. Failure retains the old profile. A leftover temp file after a power loss
is ignored and removed by the next commit. LittleFS data/metadata block overhead
is additional to the payload budget; an 8 KiB reserve is enforced against current
usage. A full filesystem rejects imports rather than deleting other data.

Slots are scanned on demand, not as a startup requirement. Each file is size
checked and independently validated. A bad file is skipped and reported in
`corrupt_slots`; a validated import can reuse its slot. Read/allocation failures
abort imports instead of being mistaken for a free slot. Externally created
ambiguous duplicate ids cannot be exported, replaced or deleted via profile APIs.
Factory reset/the inherited arbitrary file administration can remove profile
files just as other LittleFS files; those existing administrator tools are not a
security boundary. Profile export contains no configuration/secrets. Back up
profiles individually; the configuration backup alone does not include them.
There are no telemetry-history or polling flash writes.

## API and UI

All endpoints use the existing administrator authentication, including reads.
HEMS Settings contains a compact Modbus Profiles card. It displays identity,
vendor/model, built-in/imported origin, schema, source/revision, author/license,
entry count, read-only status and absence of control metadata. Import and validate
select a `.json` file; replacement and deletion require explicit UI confirmation.
View shows normalized JSON; export uses the existing authenticated blob-download
pattern. Sample decode accepts a telemetry key and 1–4 bytes in hex.

| Method and endpoint | Behavior |
|---|---|
| GET `/api/hems/profiles` | List bounded summaries, limits and corrupt-slot count |
| POST `/api/hems/profiles/validate` | Raw `application/json`, validate without storage |
| POST `/api/hems/profiles/import` | Raw `application/json`, validate/normalize/commit |
| POST `/api/hems/profiles/import?replace=ID` | Explicit replacement of that imported id |
| GET `/api/hems/profiles/export?id=ID` | Normalized JSON attachment, stable identity/provenance |
| POST `/api/hems/profiles/delete?id=ID` | Imported profiles only |
| GET `/api/hems/profiles/decode?id=ID&key=KEY&hex=0064` | Deterministic sample decoding, no network I/O |

JSON upload uses the established ESPAsyncWebServer body callbacks instead of the
existing generic filesystem upload endpoint: authentication, content type,
Content-Length, maximum size and single-upload ownership are checked before
allocating its buffer. Incomplete/out-of-order bodies fail. Request destruction
frees the buffer and releases ownership, including interrupted uploads. Short
mutex-protected profile operations reject concurrent operations as busy; neither
scheduler nor WARP worker uses that mutex. HTTP responses are bounded by known
summary/profile sizes. Errors are useful English validation diagnostics within
the translated UI, and return HTTP 400/404/413/415/503 as appropriate.

## Tinkerforge research and licensing

Inspected current upstream on 2026-10-11, pinned revision
[`a114447c4582456d123a0832eaa2d3ce5dd29f88`](https://github.com/Tinkerforge/esp32-firmware/tree/a114447c4582456d123a0832eaa2d3ce5dd29f88/software/src/modules/batteries_modbus_tcp).
Manufacturer definition files inspected include Alpha ESS, Deye, Growatt, Hailei,
Kostal, SAX Power, SMA, Solis, Sungrow and Victron Energy, plus `prepare.py` and the
battery runtime headers. No upstream source code or vendor register map is shipped.

Findings:

- These battery definitions mostly describe semantic control sequences and
  discovery/readback, not a portable standalone telemetry catalog.
- Kostal explicitly uses `ReadHoldingRegisters` and `f32_values` for readback;
  its `f32be`/`f32le` helpers and endian variants justify FC3 and both word orders.
- SMA uses unsigned 32-bit high-word-first values; Victron uses signed 16/32-bit
  values and scaled units; Sungrow uses unsigned 16-bit settings. The v1 scalar
  types and scale/offset cover these representations, without copying commands.
- The generator recognizes coils and other function codes, but the inspected
  manufacturer definitions do not evidence a required coil telemetry read.
  FC1 is deliberately absent. FC2/FC4 remain supported because WARP requires them.
- Function codes 5/6/15/16, mode-dependent branches, register comparisons,
  device discovery, watchdog refresh and write sequences cannot be translated
  safely by a blind converter. Converter work is deferred to a future focused PR.
- Whole-catalog compatibility is not claimed. Gaps include programmable control
  sequences, enum/bitfield normalization, 64-bit totals, strings and device-specific
  invalid markers beyond scalar sentinels. Revisit these only with documented
  telemetry needs before selecting a real storage profile in PR #5.

Licensing was checked rather than assuming the historical GPL label. The current
`software/LICENSE` is LGPL-2.1. Relevant C++ module headers explicitly say GNU
Lesser GPL version 2 or later; manufacturer Python files and `prepare.py` lack
individual license headers, so their exact distribution/attribution terms need
review before generated profiles are distributed. No converter or derived vendor
profiles are included. New code is GPL-2.0-or-later like existing HEMS code. No new
firmware or WebApp dependencies; ArduinoJson remains the existing 7.4.3 pin (MIT).
Native CI checks out that same version for production parser tests.

## Verification and reference hardware validation

Native tests exercise production parsing/normalization, representative/minimal
profiles, invalid schemas/fields/ids/strings/transport/count/address/scaling,
duplicate keys/entries, entry/file limits, endian/signed/float decoding, valid zero,
range/sentinel/NaN/truncation rejection, round trips and provenance, reserved ids,
explicit replacement, failed-commit preservation, deletion, corrupt-slot isolation
and count limits. FC3 protocol tests verify framing, length/function/header guards,
exceptions and rejected write codes. Existing WARP/client tests remain unchanged,
including production transport deadline, fragmentation, DNS and recovery tests.
Store tests use a deterministic backend; actual LittleFS power-cut behavior is
not proved by those tests. Native ASan/UBSan is supplementary; leak checking is
unavailable in the sandbox. The existing production configuration compatibility
suite is also run. Web lint, formatting, type checking and production build are
required; no maintained browser test infrastructure exists in this repository.

Reference validation passed on `generic_esp32s3_usb`, ESP32-S3, 8 MB, no PSRAM:

- [x] Boot; existing OpenDTU config and WARP PR #3 settings preserved.
- [x] HEMS Settings/Info and profile card work.
- [x] Import valid JSON; profile survives reboot.
- [x] Export returns expected normalized data, preserving schema/id/provenance.
- [x] Malformed/duplicate-id profiles are rejected without persistence.
- [x] Failed validation during explicit replacement preserves the good profile.
- [x] Delete imported profile; built-in remains intact.
- [x] Sample decoding shows valid zero, decimal scaling and invalid/null correctly.
- [x] Repeated import/delete/replacement leaves free heap/largest block healthy.
- [x] MQTT, Power Meter, Hoymiles communication work; DPL remains responsive.
- [x] Existing WARP configuration remains intact after profile operations and reboot.

The intrusive power-cut-during-rename and interrupted/concurrent-upload cases were
not physically forced on the reference unit. Their bounded cleanup/preservation
paths remain covered by implementation review and native deterministic tests and
are accepted as residual risk for this read-only milestone. No Marstek hardware is
required for PR #4. Deferred PR #3 physical WARP testing remains tracked separately
and does not block PR #4.

Reference resource observations during repeated import/delete/replacement were
stable: free heap 142 kB before and 143 kB after, largest free block 100 kB before
and after, fragmentation 29% to 30%, and maximum heap usage unchanged at 172 kB
(61%). LittleFS remained 156 kB free / 36 kB used. No leak or progressive
fragmentation signal was observed.

## Local resource measurement

Same `generic_esp32s3_usb` environment, 8 MB, no PSRAM, existing WebApp/dependency
pins. Baseline uses the merged PR #3 functional source (the first PR #4 commit is
roadmap-only). Application flash includes the embedded compressed WebApp:

| Measurement | PR #3 baseline | PR #4 local candidate | Delta |
|---|---:|---:|---:|
| Application flash | 2602009 B | 2630965 B | +28956 B |
| Static RAM | 100096 B | 100128 B | +32 B |

GitHub merge-ref build metadata may shift the final artifact size slightly.
No persistent profile RAM or polling worker is added. Imports require temporary
heap as bounded above. Reference hardware heap/largest-block validation passed as
documented above.
