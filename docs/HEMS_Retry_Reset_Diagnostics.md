# Native refresh and previous-boot diagnostics

This pass starts at PR #23 head `68a73486a7fbbcdf15d9fba16eb43f5618c1689f`.
The reported hardware build was merge commit `afee0d2`. No captured evidence
identifies its reboot cause. In particular, neither the Energy worker nor an
intentional application restart can be blamed from the old CPU reset text.

Verified HTTPS, one serial 10 KiB Energy/Optimizer worker, sequential solar planes,
complete-site publication, retained accepted data on refresh failure, permissions
and read-only Shadow planning are unchanged. No new device writes or flash-based
breadcrumb writes are introduced.

## Retry model and exact rules

Previously `runPending()` recorded an attempt before checking network availability;
`failed()` also recorded an attempt. Native manual refresh required the ordinary
refresh interval to be due. A boot-time local failure therefore imposed the same
3600/7200-second cadence as an actual successful provider refresh.

The provider now tracks three independent constraints: ordinary successful-refresh
cadence, local/transport spacing, and a server/quota hold. Automatic refresh waits
for all applicable constraints. Manual refresh ignores only ordinary cadence.
Configuration changes for the same provider cannot shorten an existing hold.
An obsolete in-flight generation cannot publish data, but its actual requests
still count for spacing and server/quota accounting.

| Event | Automatic retry / hold |
| --- | --- |
| Network unavailable, clock invalid, local allocation/task failure | 5 seconds; no ordinary provider interval |
| Disabled, non-polling or invalid polling configuration | No outbound work; manual rejection explains prerequisite |
| DNS or pre-response transport failure | 15, 30, 60, 120, 240, then 300 seconds maximum; reset failure count after success |
| HTTP error, decode/normalization rejection after a response | At least 300 seconds |
| HTTP 429 | At least 7200 seconds, or longer `Retry-After` |
| HTTP 503 | At least 900 seconds, or longer `Retry-After` |
| Forecast.Solar remaining quota = 0 | At least 7200 seconds, or longer reported quota period / `Retry-After` |
| Other explicit `Retry-After` on any HTTP response, including 200 | Full indicated hold; delta seconds and IMF-fixdate supported |
| Successful complete refresh | Ordinary interval: max(configured, 3600 tariff / 7200 solar / 60 normalized-HTTP seconds); 15-second manual request spacing |

Five seconds bounds local polling without locking out boot recovery; recovery is
normally within that delay plus the existing one-second scheduler tick. The worker
is not allocated just to report an unavailable network. Transport delays limit
repeated connection attempts while permitting diagnosis within minutes. Provider
response failures get a longer five-minute delay because a server was reached.
The existing five-second pause between solar planes remains. Quota/server holds
are never shortened by the fallback delays or local failures. Extremely long
server holds remain conservative across the 32-bit millis wrap; they may last
longer, never intentionally shorter. Scheduling is RAM-only, as before.

Authenticated POST `/api/hems/energy/{tariff,forecast}/refresh` returns 202 when
queued, or 409 with stable `reason` and `retry_after_seconds`. Rejection reasons:
`busy`, `not_enabled`, `invalid_config`, `clock_invalid`, `network_unavailable`,
`local_retry`, `transport_backoff`, `provider_backoff`, `rate_limited`.
Busy covers either provider queued/running on the shared worker. Clock/network
prerequisites remain mandatory for manual refresh. The GET response includes
`manual_refresh.allowed/reason/retry_after_seconds/next_allowed`, `next_refresh`
for automatic scheduling and `request_stage` (local prerequisite, DNS, transport,
HTTP response, success). A zero/unavailable deadline does not predict when network,
clock or an already-running operation will become ready. UI errors show the
specific reason and retry delay; manual refresh cannot bypass any server hold.

## Intentional restart audit

Every application ESP restart uses `RestartHelper`. All current call sites now
specify a source; its default argument preserves API compatibility. Scheduling
sets `restart_requested`. Immediately before the existing `ESP.restart()` call it
sets `intentional_restart`. LED/display handling and the existing two scheduler
iterations at a one-second interval are unchanged.

| Source / call site | Trigger | Unattended during normal runtime? |
| --- | --- | --- |
| `inverter_unresponsive` / `PowerLimiterInverter::update()` | Accumulated inverter update timeout/failure count >=20 | **Yes. First-class candidate.** The >=10 inverter-only restart and >=20 system-restart policies, counters and timing are unchanged. Not simply prolonged overnight unreachability; see existing policy comments. |
| `maintenance_reboot` / maintenance API | Authenticated reboot request | Requires a request; an external automation client could make it |
| `firmware_update` / firmware upload finish | Authenticated upload completion | Requires upload; existing completion policy can schedule restart even after an update error (factory-image rejection excluded) |
| `configuration_reset` / delete-all-files API | Authenticated configuration reset | Requires a request |
| `file_restore` / file upload finish | Authenticated restore/upload completion | Requires a request |
| `pin_mapping_change` / device configuration API | Changed pin mapping | Requires a request |

No periodic maintenance ESP reboot or automatic firmware download/apply was found.
PowerLimiter daily restart, MQTT and power-API inverter restart commands target
the inverter, not the ESP. The network reconnect watchdog resets WiFi hardware,
not the MCU. These conclusions come from repository-wide searches of source,
headers, vendored/dependency libraries, framework sources and the linked ELF.

Framework-only restart paths include `EspClass::restart()` -> `esp_restart()`,
USB OTG `usb_persist_restart()` (CDC bootloader/1200-baud/line-state handling),
FirmwareMSC and ArduinoOTA auto-reboot. The latter three are not linked into the
reference firmware; `generic_esp32s3_usb` uses `ARDUINO_USB_MODE=1` HWCDC, not
TinyUSB CDC. Host USB/JTAG reset or reset-pin/power events remain possible external
reset paths. On targets enabling other framework reboot paths, the shutdown hook
can show `esp_restart_called` without an application intentional source.

Implicit crash paths include SDK/library assertions and `abort()`, CPU exceptions,
stack canary/watchpoint/compiler protector failures, heap-corruption checks and
watchdog panic/reset paths. Vendored Frozen has assert/abort fallbacks; dependency
FastScheduler/ghostl has assertions. `Task::abort()` / web-request/socket `abort()`
stop that object, not the CPU. Compile-time `static_assert` cannot cause a runtime
reset. No direct application runtime abort/assert restart policy was found.

## Authoritative reset reason and retained record

System status preserves legacy `resetreason_0/1` text and adds integer
`resetreason_0_raw/1_raw`. `reset_diagnostics.esp_reset_reason` and its numeric code
come directly from IDF's `esp_reset_reason()`, with exact enum names including
`ESP_RST_SW`, `ESP_RST_PANIC`, `ESP_RST_INT_WDT`, `ESP_RST_TASK_WDT`,
`ESP_RST_WDT`, `ESP_RST_BROWNOUT`, `ESP_RST_POWERON` and the remaining 4.4 enums.
No custom interpretation replaces IDF's mapping. Firmware Info displays both
levels and expandable bounded previous/current-boot JSON.

On ESP32-S3, IDF 4.4.7 uses a hint in `RTC_CNTL_STORE6_REG` to distinguish a panic
or watchdog panic that ends in a software CPU/core reset. The hint is consumed
and cached before `setup()`. This is why raw `rtc_get_reset_reason()` alone was
insufficient. Panic restart bypasses `esp_restart()` and its shutdown handlers;
a normal `esp_restart()` invokes them. A registered shutdown handler records that
call independently of the intentional application marker. Hook registration
success is itself retained. Its absence is not proof that no reset occurred.

`RTC_NOINIT_ATTR` stores two 132-byte records (**264 bytes total RTC slow RAM**),
not flash. The linker places it in `.rtc_noinit (NOLOAD)`; startup does not clear
it. Magic, version, size, enum/flag validation and checksum reject invalid/stale
layouts. The new slot's magic commits last, with memory fences; an interrupted
write can fall back to the preceding valid slot. A checksum is corruption detection,
not proof against every possible corruption. First firmware boot may have no valid
record. An OTA layout change may also invalidate it safely.

At initialization the latest valid record is copied into immutable `previous_boot`
DRAM state before starting `current_boot` with a new generation. Generation counts
retained sessions, not power cycles. Six fixed activity slots cover Energy/Optimizer
(shared worker), firmware update, configuration, network, maintenance, runtime.
Each stores token/sequence, active flag, operation, phase, sub-index and checkpoint
uptime. Scope completion sets inactive/complete, including operations that return
an error normally. Interrupted work stays active. Completed unrelated operations
cannot hide an active worker; all active slots are also serialized. Latest activity
per slot replaces older activity; this is coarse localization, not task tracing or
a proof of fault attribution. Uptime is the last checkpoint time, not reset time.

Energy checkpoints cover provider start/plane index, DNS, connect (TCP **and** TLS
inside one library call), HTTP, decode and aggregation. The library cannot split
every TCP/TLS/timeout failure: preserve its exact negative HTTP/TLS codes and bounded
texts. Existing phase memory samples remain unchanged. Global checkpoints cover
boot/configuration, network reconfiguration/reconnect, actual runtime-data writes,
firmware upload, restore/reset and maintenance. No per-loop tracing/history buffer.

### Retention guarantees and limits

| Reset class | Handling and evidence |
| --- | --- |
| `ESP_RST_SW`, `ESP_RST_PANIC`, `ESP_RST_INT_WDT`, `ESP_RST_TASK_WDT` | Expected retained RTC memory; matching IDF tests explicitly check RTC no-init values after these resets |
| `ESP_RST_WDT` | Best effort; matching IDF tests cover RTC WDT, but not every possible full-system/RTC-domain watchdog failure |
| `ESP_RST_DEEPSLEEP` | Conditional on RTC slow-memory power retention; firmware does not use deep sleep; the matching S3 SDK deep-sleep test is disabled |
| Power-on, brownout, external, SDIO, unknown | Previous record deliberately discarded; do not trust apparent RAM contents across uncertain power/domain retention |

No mechanism can guarantee this record survives RTC power loss, severe corruption,
reset during initialization, or every watchdog escalation. No extra persistent
flash writes or core-dump partitions were added. Absence of the intentional marker
means no marker was recovered, not an independently proven cause.

## Actual panic/watchdog configuration

Measured target: espressif32 6.10.0, Arduino-ESP32 2.0.17, ESP-IDF **4.4.7**,
`tools/sdk/esp32s3/dio_qspi/include/sdkconfig.h` (matching relevant qio settings).
These are the installed prebuilt SDK settings, not assumed IDF defaults:

| Setting | Effective value / implication |
| --- | --- |
| `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT` | 1: print panic/registers/backtrace, then reboot automatically; no silent reboot, halt or GDB stub mode selected |
| `CONFIG_ESP_INT_WDT`, `TIMEOUT_MS`, `CHECK_CPU1` | 1 / 300 / 1: interrupt watchdog on both CPUs; panic or hardware fallback can reset |
| `CONFIG_ESP_TASK_WDT`, `PANIC`, `TIMEOUT_S` | 1 / 1 / 5 |
| `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` | 1; CPU1 idle check not selected |
| `CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY` | 1 |
| `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK` | 1 |
| `CONFIG_COMPILER_STACK_CHECK_MODE_NORM`, `STACK_CHECK` | 1 / 1: normal compiler stack protector, not all functions |
| `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE`, assertion level | 1 / 2 |
| `CONFIG_HEAP_POISONING_LIGHT` | 1; allocation failure abort option not selected |
| `CONFIG_BOOTLOADER_WDT_ENABLE`, `TIME_MS` | 1 / 9000; disabled before application start (`DISABLE_IN_USER_CODE` not selected) |
| `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` | 1, ELF/CRC32, boot check, max64 tasks, 1024-byte dump stack in SDK; **no core-dump partition in project layout**, so no usable persistent dump. SDK checks missing/invalid partition and returns before erase/write |
| Console | UART0 at115200 + secondary USB Serial/JTAG; panic printer writes both |

Arduino loopTask's optional task-WDT subscription is false; application does not
enable it. AsyncTCP explicitly undefines/disables its subscription in the installed
patched library. espMqttClient defaults `EMC_USE_WATCHDOG=0`, with no project override.
CPU0 idle task and SDK watchdogs still operate; absence of per-task registration
does not prevent interrupt/starvation watchdogs or stack protection from firing.
Normal code uses `CORE_DEBUG_LEVEL=5`. Capture raw Serial output continuously and
retain the exact matching ELF for address decoding. USB reconnect/reset may lose
the end of a panic; UART0 is a useful capture alternative. No panic/WDT policy was
changed in this pass.

SDK source evidence (matching official v4.4.7, commit
`38eeba213aa695aabfd6d89aa9f5078dbe5a94c3`):

- [`esp32s3/reset_reason.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/port/soc/esp32s3/reset_reason.c): enum mapping and reset hint.
- [`panic.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/panic.c), [`esp_system.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/esp_system.c): panic/noos versus shutdown handlers.
- [`test_reset_reason.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/test/test_reset_reason.c): reset-class RTC retention tests and S3 exclusions.
- [`esp32s3/memory.ld.in`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/ld/esp32s3/memory.ld.in), [`sections.ld.in`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/ld/esp32s3/sections.ld.in), [`cpu_start.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/port/cpu_start.c): RTC placement/startup.
- [`core_dump_flash.c`](https://github.com/espressif/esp-idf/blob/v4.4.7/components/espcoredump/src/core_dump_flash.c): missing-partition guards.

## Validation and resources

Deterministic tests exercise production scheduling, boot network/clock preflight,
five-second recovery for both providers, queued/running exclusion, manual bypass
of ordinary cadence, all rejection classes, exponential cap/reset, HTTP Retry-After
and quota holds, generation changes, retained data, one/two/six solar planes,
transport errors and TLS wiring. Journal tests cover encode/decode, invalid/torn
records, previous boot, concurrent activity, completed/interrupted operations,
restart source encoding and IDF enum serialization. A production RestartHelper test
checks both existing scheduler ticks, LED/display handling and recovery of
`inverter_unresponsive` after a simulated reboot. CI requires no live providers.

Local checks include all31 host executables and no-write surface guard, production
configuration compatibility/round-trip, firmware-channel checks, Admin/auth and
tariff/refresh UI tests, frontend type/build/lint/format, cpplint and the reference
firmware. GitHub CI is authoritative for the eight-target HEMS matrix.

Comparable reference builds (`PLATFORMIO_BUILD_FLAGS=-fstack-usage`, frontend built):

| Resource / compiler frame | Previous head | This pass |
| --- | ---: | ---: |
| Flash sketch | 2798193 | 2807125 (+8932 bytes) |
| Static RAM reported by PIO | 115896 | 116240 (+344 bytes) |
| Additional RTC no-init record | 0 | 264 bytes (not counted in PIO ordinary RAM total) |
| Energy worker allocation | 10240 | 10240 |
| Energy worker entry | 48 | 48 |
| `runPending` | 592 | 608 |
| `fetch` | 336 | 352 |
| Native helper / transport / diagnostic connect | 400 / 384 / 64 | unchanged |
| Normalized HTTP helper | 2080 | 2080 |
| Optimizer `run` | 624 | 640 |

The extra16-byte frames carry small scope/control state; native TLS lifetimes and
Series/optimizer allocation reductions remain intact. Journal functions use32–48
byte compiler frames; no heap allocation or logging in checkpoint commits.
System-status JSON is allocated only during an API read, outside the RTC lock.
These frame sizes do not prove runtime stack margin or minimum heap.

## Next hardware run: minutes

1. Flash the PR's non-PSRAM `generic_esp32s3_usb` OTA image; record its tested merge
   SHA. Start a continuous115200 Serial capture, keep matching ELF. Leave Shadow
   Optimizer disabled until native provider stability is established.
2. Boot and wait for clock/network. If `network_unavailable` occurs, confirm zero
   outbound metrics and a five-second local retry, then see a request within roughly
   six seconds of readiness. No hour/two-hour wait or manual reboot is needed.
3. Fetch Energy-Charts manually (or inspect the initial automatic success); after
   the15-second spacing, Refresh now should work despite the one-hour automatic
   deadline. A blocked request must show its specific reason/delay.
4. Manually fetch configured two-plane Forecast.Solar when status says allowed.
   Expect sequential planes with the existing five-second pause, complete aggregate
   only after both succeed. Respect any explicit quota/server hold; do not hammer.
5. Save authenticated `/api/hems/energy/tariff`, `/forecast`, and
   `/api/system/status` JSON. Capture both plane records, negative codes/text,
   DNS/TLS fields, payload/JSON peak, before/connected/decoded/released heap and
   worker watermark. Prefer >=3 KiB stack free (accept >=2 KiB), minimum internal
   heap >=25 KiB and largest block >=20 KiB. Watch released heap for progressive loss.
6. If uptime drops/reboot occurs, **immediately save system status before another
   intentional reboot**: IDF reason/code, both raw RTC reasons, previous validity,
   requested/intentional marker and `restart_source`, shutdown-hook flag, selected
   subsystem/operation/phase/index/checkpoint uptime and active operations. Save the
   Serial panic/backtrace if present. `inverter_unresponsive` identifies the existing
   PowerLimiter policy; an active Energy breadcrumb alone does not establish cause.

Real ESP32 retention, panic capture, runtime margins, the former second-plane
failure and the observed reboot remain hardware validation questions.
