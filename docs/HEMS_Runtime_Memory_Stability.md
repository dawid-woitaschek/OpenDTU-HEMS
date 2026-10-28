# Full HEMS runtime/memory stability

PR #35 (`fix/hems-runtime-memory-stability`), based on `hems`
`693eafe60fb2538ec11019dc0875737eb7e0495c`, is merged at
`f54da01e5fb5086fe5fdf448f7c4dd2e11ecdc3c`. The investigation and build figures
below describe that runtime change. Refreshed PR #31/CMT and PR #34/energy
management are merged on this base. Their new behavior remains physically
unvalidated beyond previously recorded evidence.

## Physical evidence supplied for this work

Device A is an ESP32-S3, Fusion Board v2.1, 8 MB flash, physically 2 MB quad
PSRAM. Its PSRAM environment is `generic_esp32s3_usb_quad_psram`.

| Observation on the same hardware/configuration | No PSRAM enabled | 2 MB PSRAM enabled, initial cycles |
| --- | --- | --- |
| INTERNAL minimum free | About 5.8 kB during fresh-boot HTTPS; later as low as 320 B | About 51.9 kB |
| Largest free INTERNAL block during HTTPS | Fell to roughly 7.7 kB | Around 47–51 kB during Forecast.Solar HTTPS |
| Failed allocations | 28 in one long run | None in initial cycles |
| Unexpected panic resets | Two | Long-term behavior not yet established |

PSRAM was actively used after enabling it. The energy worker still had about
3.4 kB historical free stack. These are supplied physical observations, not
measurements made by this PR. The earlier PR #30 >24 h successful no-PSRAM run
remains historical evidence, but the later pressure/failures make no-PSRAM Full
HEMS unacceptable as the design/acceptance target. **PSRAM is the expected Full
HEMS memory tier.** Existing no-PSRAM environments remain buildable compatibility
targets; their eventual compatibility/deprecation policy is separate. A strong
initial improvement with 2 MB PSRAM alone did not prove long-term stability.

Subsequent user-supplied physical validation exercised PR #35 on Device A with
2 MB PSRAM for approximately 21 hours without allocation failures, unexpected
resets, heap drift, fragmentation drift or critical stack loss. This is accepted
as sufficient for the current project stage. It does not establish every future
provider/network scenario or validate the subsequently refreshed CMT behavior.

## Implementation and source-derived implications

- Keep the bounded global DP optimizer, existing serial worker, task stack sizes,
  provider limits, atomic data publication and verified TLS behavior.
- Explicit PSRAM placement for dynamically allocated Series (provider/parser/
  aggregate/planner snapshots), optimizer Workspace and candidate/retained Result,
  and bounded provider JSON storage, including the small transport filter. These
  objects need neither DMA nor stack/internal placement. Static provider Series
  stay in their existing storage; sockets, TLS and task stacks retain their library
  placement. This is deliberately a scoped change rather than a global allocator.
- `hems/Memory.h` requests `SPIRAM|8BIT` when that capability heap exists. Exhaustion
  returns failure rather than spilling these objects into INTERNAL. Without PSRAM,
  the helper requests normal 8-bit heap for compatibility. JSON reallocation uses
  the same capabilities and preserves the original allocation/accounting on failure.
  The JSON budget counts requested data bytes, as before, excluding allocator metadata.
- Keep `heap_caps_malloc_extmem_enable(512)` unchanged as the general malloc safety
  net. It prefers INTERNAL for smaller requests and external memory for larger
  requests, with fallback; it does not guarantee placement. Device A's improvement
  occurred with that policy already present. There is no comparative threshold
  evidence supporting a change, and explicit small JSON-string placement now avoids
  reliance on the threshold for provider documents.
- A per-plane Series now dies before the quota delay/next request. Previously the
  previous plane's ~5 KiB scratch survived subsequent requests in cycles of three
  or more enabled planes, and the final inter-plane delay after a merge. The site
  aggregate still survives as required. JSON/HTTP/TLS scratch already died before
  aggregation; retain that boundary. Aggregation failure preserves accepted data.
- Catch only `std::bad_alloc` around the entire native request, including Arduino
  constructors and header allocation. RAII destroys request objects before returning
  `insufficient_memory`; clear partial JSON. Arduino's outer nothrow new alone cannot
  catch constructor-internal throwing new. Other exceptions propagate. This cannot
  recover asserts, corruption, unchecked library C allocations, or an exception
  runtime unable to allocate its own exception storage under absolute exhaustion
  (the pinned SDK has a zero-byte emergency exception pool).
- Replace byte-at-a-time socket reads/checks with a fixed 256-byte stack window,
  reading no more than currently available, Content-Length and MaxPayload+1.
  ArduinoJson still streams; there is no full-response buffer. The idle wait is
  bounded at 5 s with scheduler-friendly delay; a 15 s elapsed deadline starts
  before GET and also bounds the body. GET itself retains library connect/TLS/read
  timeouts; this is not a preemptive cancellation of library GET/DNS/handshake calls.
  Buffered bytes count as consumed when delivered, preserving parser/trailing-body
  accounting. Successful bodies are drained through their exact boundary; an
  unknown-length body needs EOF to distinguish exact-limit from limit+1. No socket
  read goes beyond Content-Length. A malformed body may stop early as before.
- The existing 16-checkpoint/four-failure journal now correlates INTERNAL and
  `SPIRAM|8BIT` free/minimum/largest, allocated bytes and block counts. Fragmentation
  percentages are derived from free/largest at serialization, not stored. Request
  before/connected/decoded/released samples also expose PSRAM free/minimum/largest.
  Absent PSRAM yields zero values. Minimum values remain since boot, not per request.
  Failure rows add cached last-checkpoint PSRAM free/largest, explicitly not readings
  taken at failure time. No allocation, heap walk or JSON was added to the failure
  hook; checkpoint heap walks run outside its lock. History size stays bounded.

## TLS trust-store investigation: unchanged

The shared block contains seven roots: ISRG X1/X2, DigiCert Global Root G2 and
GTS R1/R2/R3/R4. The pinned Arduino-ESP32 2.0.17 `ssl_client.cpp` passes the entire
PEM block to `mbedtls_x509_crt_parse` per connection, then frees parsed CA storage
following a successful verified handshake (and on teardown/error paths). The
pinned ESP32-S3 SDK uses `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=1` and 16 KiB maximum
record content, so root parsing can contribute INTERNAL peak allocations/CPU, but
post-connect heap samples cannot isolate its transient peak from the handshake.
The static PEM itself is const flash data: 9,376 B including its terminator,
with 6,640 B summed DER across the seven certificates (host byte counting, not
parsed heap usage).

No ESP32 A/B measurement of parse heap, fragmentation or CPU has been performed.
A host parse or one observed provider chain would not establish target benefit or
robustness across provider/CDN chain rotation. Consequently the benefit of reducing
anchors is **unquantified**, and no roots are removed. Hostname validation, SNI,
normal CA verification and redirect rejection remain intact; no leaf/intermediate
pinning. Follow-up only if target measurements show meaningful benefit: instrument
root parsing/handshake on Device A, compare complete vs proposed sets, verify both
providers' actual chains across endpoints/rotation, and document root rollover and
firmware maintenance requirements before changing policy.

## INTERNAL reserve investigation: unchanged

The pinned quad-PSRAM SDK sets `CONFIG_SPIRAM_USE_MALLOC=1`,
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`,
`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`, and
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=1`. The runtime 512-byte call overrides the
malloc preference threshold. IDF's reserve is a pool protected from general malloc
for INTERNAL/DMA capability requests, not extra memory or an application/TLS
reservation. `esp_spiram_reserve_dma_pool()` exists, but carving a pool late after
Arduino startup is not equivalent to selecting the SDK's boot configuration.
Changing SDK config headers/build flags alone does not rebuild precompiled IDF
libraries. No reserve size has a measured benefit here; carving out INTERNAL can
also restrict other callers. Leave it unchanged. Evaluate early-boot configuration
and target A/B measurements separately if explicit placement still leaves pressure.

## Validation boundaries

Software: `make -C test -j4 test` passed all 35 executables plus the control-surface
Python guard, using ArduinoJson 7.4.3 and network permission for the existing
loopback socket tests. The changed transport was rebuilt/retested afterward to
include the final actual millis-wrap case. Coverage includes PSRAM capability
selection, no INTERNAL spill, bounded JSON realloc failure accounting, library
constructor/header/body bad_alloc recovery and RAII, arbitrary-exception propagation,
reader short reads/idle/GET+body deadlines/limits/truncation/trailing bytes,
Content-Length isolation, one/two/six-plane lifetimes and allocation-failure atomic
retention, diagnostic ring bounds/serialization, and existing global DP/controller/
dispatch regressions. No frontend code changed or frontend validation was claimed.

Compile/link: local PlatformIO builds use espressif32 6.13.0, Arduino-ESP32 2.0.17,
Xtensa GCC 8.4.0. `GITHUB_ACTIONS=true` skips the frontend hook; baseline and changed
builds embed the same pre-existing local webapp assets. These are compile/link
checks, not release/OTA artifact validation. Changed builds additionally use
`PLATFORMIO_BUILD_FLAGS=-fstack-usage`, which emits compiler frame reports without
changing task stack sizes.

All three changed firmware environments compiled/linked successfully. A separate
baseline checkout at `693eafe6` provides the same-target quad comparison:

| Environment / source | Reported RAM, B | Reported flash, B |
| --- | ---: | ---: |
| quad PSRAM, baseline | 117,732 | 2,799,941 |
| quad PSRAM, changed | 118,564 | 2,802,341 |
| octal PSRAM, changed | 118,476 | 2,803,493 |
| no-PSRAM USB, changed | 118,304 | 2,799,913 |

Quad delta: **+832 B RAM, +2,400 B flash**. Symbol sizes account for +440 B in the
journal and +384 B in EnergyData; the remaining 8 B is section/layout overhead.
Flash totals also include build metadata. These static figures are not free-heap
or TLS-peak measurements. Changed builds compiled the PR source before publication;
their embedded metadata is not a validated release/OTA identity.

Xtensa compile-only `sizeof` probing of production types reports Series 5,184 B,
Workspace 15,768 B and Result 16,896 B. These sizes remain unchanged. At replanning,
two Results (retained/candidate), Workspace and two Series can overlap: 59,928 B
of explicitly placed bounded data, excluding allocator metadata. The old >512-byte
malloc policy already preferred PSRAM for these large objects; this is a placement
and exhaustion guarantee, **not a measured 59,928 B INTERNAL saving**. Provider JSON
small allocations now also select PSRAM explicitly. Per-plane scratch release removes
5,184 B from overlap with third/later HTTPS and intervening post-merge quota waits.
The fixed heap journal grows from 956 to 1,396 B (+440 B); additional request memory
samples add 384 B to the retained EnergyData service. The reader adds a fixed stack
window, not a heap payload buffer.

Selected compiler frames (baseline recompiled separately with `-fstack-usage`;
changed USB/quad builds, same compiler/SDK):

| Frame, B | Baseline | Changed |
| --- | ---: | ---: |
| providerGet / inner request combined | 432 | 32 + 768 |
| nativeRequest | 416 | 432 |
| fetch | 368 | 416 |
| runPending | 608 | 608 |

This adds 432 B along that selected HEMS call path, excluding library/callback
frames. It is not a measured worker watermark. The 10,240 B worker stack remains
unchanged; the later Device A run reported no critical stack loss, without supplying
a new numeric watermark here.

These results cannot establish actual free heap, PSRAM timing, Wi-Fi behavior or
long-term stability on ESP32. CI runs independently; no remote workflow polling
was performed.

For further physical coverage use **Device A, `generic_esp32s3_usb_quad_psram`**. Capture
boot and quiet worker-idle baselines plus before-connect/connected/response/parsed/
released checkpoints, both providers and all enabled planes, including failure
records/capabilities, quotas, timestamps and stack watermark. Exercise normally
spaced refreshes, repeated DP replanning, concurrent UI/network/TLS load, network
loss/timeout/reconnect and repeated day boundaries over a long run. Compare equivalent
quiet periods and monitor INTERNAL and PSRAM largest/free/minimum trends, allocation
failures, resets and watchdogs. Stack headroom should remain at least 2 KiB (prefer
3 KiB); no stack reduction is proposed. Earlier working targets of 25.6 kB INTERNAL
minimum and 20.48 kB largest during HTTPS are validation targets, not guarantees.

PR #34's larger Result/Plan, Workspace, Series and optional learning snapshot now inherit this allocator. Its fixed retained learner stays static and bounded. See [energy-management integration evidence](HEMS_Energy_Management.md); the measurements above describe PR #35, not the combined build.
CMT radio changes, trust-set pruning, SDK/TLS rebuilds, reserve tuning and no-PSRAM
removal/deprecation remain outside this PR.
