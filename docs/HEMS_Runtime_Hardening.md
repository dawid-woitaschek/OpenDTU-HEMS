# Native energy runtime hardening

For the subsequent retry/reset pass and the current provider-first hardware procedure,
see [retry and previous-boot diagnostics](HEMS_Retry_Reset_Diagnostics.md).

This pass starts at PR #23 head `d87a3a2e6022d0b9e02de155d31a8bc33aa0c687`.
It keeps one serial optional Energy worker, verified HTTPS, retained provider data,
all configured enabled PV planes, conservative request spacing, and read-only
Shadow Optimizer behavior. No control/permission or economic dispatch changes.

## Evidence and remaining uncertainty

The real board's 108-byte minimum on an 8192-byte worker is unsafe. Compiler output
proves avoidable HEMS stack reservations. Source/lifetime analysis proves redundant
full-Series allocations and unnecessarily long JSON retention. It does **not**
prove what caused the second Forecast.Solar HTTPS failure. In particular, the old
transport had already destroyed TLS/HTTP objects before normalization, and each
plane's JSON document died before the next iteration; there was no proven overlap
of two TLS sessions. The JSON document did survive aggregation and the five-second
inter-plane pause. Those lifetimes are now shortened.

HTTPClient -1 does not identify a certificate failure. Arduino-ESP32 2.0.17 maps
DNS, socket creation/connect/select/timeout, and some generic secure-client errors
to the same HTTP code. `WiFiClientSecure::lastError()` retains the connect result;
its -1 is also ambiguous. It is not a reliable socket errno or TLS read/write error.
The second-request cause remains unproven: stack pressure, heap/contiguity pressure,
TCP/connect failure, TLS failure, or timeout still require device evidence.

## Compiler stack measurements and worker budget

Reference: `generic_esp32s3_usb`, espressif32 6.10.0, Arduino-ESP32 2.0.17,
Xtensa GCC 8.4.0 release build. Both builds used:

```sh
PLATFORMIO_BUILD_FLAGS=-fstack-usage GITHUB_ACTIONS=true pio run -e generic_esp32s3_usb
```

Build the frontend first when setting `GITHUB_ACTIONS=true`; this skips the normal
frontend prebuild hook. Inspect `.pio/build/generic_esp32s3_usb/**/*.su`.

| Compiler frame, bytes | Before | After |
| --- | ---: | ---: |
| Energy worker entry | 608 | 48 |
| Settings/generation snapshot (`runPending`) | in worker | 592 |
| `Service::fetch` | 2128 | 336 |
| Native URL/document/normalization helper | in fetch | 400 |
| Local normalized HTTP helper | in fetch | 2080 |
| `providerGet` | 352 | 384 |
| Diagnostic hostname connect override | absent | 64 |
| Energy-Charts parser | 112 | 112 |
| Forecast.Solar parser | 160 | 176 |
| Aggregate | 80 | 80 |
| Optimizer service run | 608 | 624 |
| Optimizer calculate | 336 | 336 |
| Optimizer live-input snapshot | 272 | 272 |
| Library HTTP sendRequest / connect | 128 / 64 | unchanged |
| Library `start_ssl_client` | 656 | unchanged |

Native HEMS frames through transport total 3088 before, 1760 after, excluding
library call depth. The diagnostic connect override adds 64 bytes while replacing
three hostname-connect wrapper frames (32 + 32 + 64). The final hostname-preserving
base connect and TLS entry remain. This suggests about 1.3 kB less native stack
pressure. The optimizer no longer carries the worker's Settings frame underneath it;
its worker/run/calculate prefix falls from 1552 to 1008 bytes.

The worker is now **10240 bytes**, a 2048-byte increase. Combining that explicit
reserve with the measured reduction suggests roughly **3.4–3.5 kB** minimum free
against the observed 108-byte watermark for the same HTTPS workload. This is a
budget rationale, **not a measured hardware watermark**. Library inlining, execution
paths, prebuilt ESP-IDF/mbedTLS code without `.su` files, and concurrent application
activity prevent proving runtime safety from frame sums. The larger isolated bridge
frame does not sit beneath native HTTPS. It has the same larger worker budget.

## Heap and full-Series audit

ESP32-S3 `sizeof(Series)` is **5184 bytes**. The normalized contract and 256-interval
capacity are unchanged.

- Accepted tariff and forecast Series remain provider-owned. Worker refreshes build
  a separate unpublished Series so failed refreshes cannot damage accepted data.
- Both native parsers validate completely before filling the supplied destination.
  Their additional heap Series and full-Series copy are removed. Failed parses leave
  the destination intact, including malformed late entries.
- The first/only solar plane fills the unpublished aggregate directly. A second
  plane scratch is allocated only when another successful plane needs merging.
  This removes another Series and the first-plane copy from one-plane/first-plane
  processing. First-plane normalization now has one Series instead of three.
- Subsequent plane normalization uses aggregate + plane scratch, rather than those
  two plus a parser candidate. JSON dies before aggregation and before the pause.
- Aggregation retains a temporary full Series: it safely handles aliased output and
  differing interval boundaries without corrupting an input on failure. Its three
  Series no longer overlap the provider JSON document.
- Normalized JSON parsing retains its existing atomic candidate. Provider acceptance
  retains its validated copy into stable storage. These copies protect correctness.
- Optimizer calculation still needs two consistent Series snapshots and its ~15.5 kB
  DP workspace. The snapshots die immediately after calculation. A published plan
  retains only 102 bytes of provenance text (104 aligned bytes), reducing retained
  result size by **10264 bytes**. Recalculation no longer overlaps old retained
  interval snapshots with new scratch. No DP algorithm or device-write changes.
- TLS/HTTP objects remain request-scoped heap objects to avoid large library frames;
  they die on every return from transport, before provider normalization. JSON/filter
  allocation is streamed; there is no full response String/buffer.

`MaxPayload=40000` is a read limit, not an allocation. `MaxJsonMemory=49152` limits
incremental allocations; constructing the real BoundedAllocator/JsonDocument uses
zero tracked bytes. ArduinoJson allocates pools/strings as needed and may reallocate
strings. The allocator peak counts requested live bytes, excluding malloc/allocator
headers and transient allocator implementation costs. Neither cap was reduced.

Production transport/filter + real allocator tests report these **host** fixture
requirements (not captured live response sizes or ESP32 allocator measurements):

| Deterministic fixture | Payload bytes | Allocator peak bytes |
| --- | ---: | ---: |
| Energy-Charts 192 intervals + ignored 8 kB field | 11708 | 8328 |
| Energy-Charts 256 intervals + ignored 8 kB field | 12924 | 12424 |
| Forecast.Solar 256 intervals / 257 UTC keys | 6795 | 21250 |

Host slot/string sizes differ from ESP32. The new `json_peak_bytes` exposes actual
on-device filtered-document usage. Separate tests exhaust a tiny allocator and
exercise the byte limit and truncated/invalid bodies. Real payload requirements
remain bounded by the original caps and must be observed on the next board run.

## Bounded transport diagnostics

Authenticated `GET /api/hems/energy/tariff` and `/forecast` now include:

- Existing actual `http_status` (including negative codes), plus `request.http_error`
  from `HTTPClient::errorToString()`, bounded to 39 characters.
- `request.dns`: `not_attempted`, `resolved`, or `failed`. A small secure-client
  override follows the pinned library's existing DNS flow once, retaining the
  original hostname and CA roots for TLS SAN verification/SNI. No IP-only TLS call.
- `request.tls_error` and bounded `tls_error_text` (95 characters) from the library's
  last connect error. Zero means no captured negative connect result; it does not
  prove that a later TLS read/write succeeded. Generic -1 remains ambiguous.
- `duration_ms`, consumed `payload_bytes`, and actual `json_peak_bytes`.
- `before`, `connected`, `decoded`, `released` memory samples: free internal 8-bit
  heap, minimum free internal heap **since boot**, largest current internal block,
  and worker stack minimum free bytes. `released` is after request JSON, TLS and
  HTTP destruction. Samples are phase measurements, **not continuous per-request
  minima**; failed connect cleanup can run before the connected sample. They cannot
  reveal the smallest contiguous block inside an uninstrumented handshake.
- `runtime_memory` reports current internal heap and the recorded worker watermark.
  The top-level worker watermark includes optimizer work too. Read it after the
  cycle completes. Before the first cycle, zero means unavailable; a zero watermark
  after worker execution needs investigation.
- Each configured plane has its own bounded latest request record, HTTP status,
  last error, quota, and last successful fetch timestamp. No history buffers/log flood.

Stable errors now distinguish DNS, connect, TLS/library errors, HTTP response errors,
read/body timeout, connection lost, and known allocation failures. Raw code/text must
be consulted before interpreting generic connect failure. Library TLS allocation
errors retain their exact code/text; HTTP -8 and JSON allocation failure become
`insufficient_memory`. No errno is invented. WiFiClientSecure timeout is expressed
in seconds; HTTPClient timeout remains milliseconds. Content-Length and closed-stream
EOF now bound body reads, avoiding the previous unnecessary stream timeout after a
complete declared-length body. Byte, nesting, trailing-content and total body-time
limits remain fail-closed; redirects remain disabled.

## Tests and resource impact

Local validation passes all 28 native executables and the semantic/no-write surface
guard; configuration round-trip/compatibility; firmware channels; Admin save/auth;
tariff UI conversion/API save/reload and canonical firmware persistence; frontend
build/type-check, oxlint, ESLint, and Prettier; reference firmware build. Forecast
service tests cover one, two, six planes, second-plane transport failure with retained
aggregate, quota stop, and generation changes during the second request and between
requests. Transport tests exercise real filtering/bounds, negative codes, DNS/TLS
errors, body timeout/truncation and six sequential requests with destroyed clients.
CI uses no live Forecast.Solar requests. GitHub CI remains authoritative for the full
firmware matrix.

Comparable local reference builds, with generated frontend embedded:

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Flash sketch | 2794781 | 2798193 | +3412 bytes |
| Static RAM | 114136 | 115896 | +1760 bytes |
| Optional Energy worker stack | 8192 | 10240 | +2048 bytes runtime heap |

The static increase is bounded diagnostic records (eight NativeInfo records grow
by 220 bytes each). Optimizer-enabled retained heap savings exceed the stack/static
increase. This is object-size accounting, not a claim about actual minimum free heap.

## Next real ESP32-S3 validation

Use the non-PSRAM `generic_esp32s3_usb` build on the Fusion v2.1, with a fresh boot.
Capture the authenticated tariff/forecast status after the first native tariff
request, after both solar planes, and after a shadow calculation with real fresh
Storage/WARP data. Respect the existing native refresh/backoff and quota floors.
If a solar request fails, save **both plane request records**, top-level last error,
worker watermark, quotas and timestamps before another attempt replaces them.

Measure worker free stack: prefer >=3072 bytes, accept >=2048. Target minimum free
internal heap >=25600 bytes and largest internal block >=20480 bytes during HTTPS.
Compare before/connected/decoded/released samples over normally spaced refreshes,
including six planes if configured, and repeated optimizer recalculation. Watch for
allocation errors, declining released heap/largest block, resets and watchdogs.
Boot minimums can be lowered by unrelated tasks and cannot isolate each handshake;
use the exact negative code/TLS text and the phase samples together. If generic -1
persists without a captured specific TLS error, the pinned library cannot distinguish
TCP/select timeout from every generic TLS failure through this API; collect its
existing bounded serial error messages for that attempt. Do not infer certificate,
fragmentation, or stack failure from -1 alone. Hardware acceptance is still pending.
