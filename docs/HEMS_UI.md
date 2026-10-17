# HEMS product UI

The firmware-hosted Vue 3 / Bootstrap UI has four peer views:

| Route | Purpose |
| --- | --- |
| `/hems` | Daily energy overview, tariff and PV charts, automation and Shadow proposal |
| `/hems/settings` | General mode, category permissions, tariff, forecast and optimizer configuration |
| `/hems/devices` | Endpoints, profile bindings, expandable manual controls and profile administration |
| `/hems/diagnostics` | Engineering telemetry, providers, control execution, optimizer rows, reset/memory diagnostics |

The existing HEMS menu and a short peer navigation row expose these views. Legacy
`/info/hems`, `/settings/hems` and `/hems/profiles` aliases remain supported.

## Daily use

Grid import/export, AC solar production, battery SoC/charge direction and EV
connection/charging appear as compact numeric cards. Fresh zero/false values are
valid; stale, unknown or disconnected values never become zero, idle or false.
Device faults/alarms and provider failures remain visible. Current and summary
numbers become unavailable after failed status reads or invalid freshness; retained
curves remain inspectable with the failure/outdated message alongside them.

The overview describes actual EV Protection enforcement separately from a Shadow
restriction. Disabled stops HEMS control; Monitor forbids automatic writes but
allows manual semantic controls for categories with HEMS Control permission;
Active permits policy writes only for permitted categories. No authorization,
protocol, control, optimizer, retry, TLS, profile or Modbus backend was changed.
The economic optimizer always carries SHADOW and no-device-writes labels.

Tariff numbers use **backend-supplied effective import prices**. The frontend only
converts integer micro-currency units for display; it does not recompute VAT or
variable costs. EUR prices use ct/kWh, VAT uses %, monthly fees use EUR/month.
Other normalized currencies keep their currency/kWh unit. Source price remains
separate and can be overlaid. If effective prices are unavailable, the curve and
covered averages explicitly identify source prices. A missing effective interval
never silently becomes a source-price segment. Daily averages are duration weighted,
marked when coverage is partial and displayed to two decimals; inspected interval
prices retain four decimal places in ct/kWh (the integer API precision).

PV totals and remaining-today energy integrate actual forecast intervals in browser
local time. Incomplete coverage yields unavailable totals, not extrapolated kWh.
There is no historical-production inference or fabricated per-plane series.
The six-plane editor uses responsive fieldsets, decimal coordinates, kWp and the
provider's south=0°, east=−90°, west=+90° azimuth convention. Persisted e6 coordinates,
watts, enabled flags, order and six-plane limit are unchanged. Edited/reordered
planes are not assigned the previous plane's provider status before saving.

## Chart choice and measured comparison

Evaluated uPlot first, including Tinkerforge's direct use. Chose a shared custom
SVG interval chart because each API series is bounded at 256 intervals and these
views need step intervals rather than large history plots. There is no new runtime
dependency or Vue chart wrapper.

| Concern | uPlot 1.6.32 | Implemented SVG |
| --- | --- | --- |
| Minified / gzip payload | 51,081 / 21,991 B JS; 1,857 / 758 B CSS, before wrapper/plugins | Isolated production chart including its used helpers: 7,272 B JS + 1,411 B CSS; 3,130 + 513 B gzip (level 9) (Vue/i18n external) |
| Maintenance | Established axes/canvas engine; custom integration still needed | One 293-line Vue component plus tested step-path/calendar helpers |
| Responsive | Resize integration required | ResizeObserver, scheduled width updates, native SVG |
| Touch / inspection | Cursor/touch configuration/plugins | Pointer inspection, horizontal drag, keyboard arrows and live inspection caption; vertical page scrolling preserved |
| Dark mode | Explicit canvas styles/redraw | Bootstrap CSS variables; no theme redraw lifecycle |
| Custom periods | Canvas hook/plugin | Thin muted semantic strip, independent primary numeric curve |
| Reuse | Suitable for all three domains | Shared tariff/PV infrastructure; reusable interval model for future complete optimizer data |

The isolated SVG size is a reproducible attribution estimate, not an exact
incremental app delta: Vue/i18n and some helpers are shared in the full build.
The complete app/resource comparison is recorded in the productization validation
report below. Both measurements use production minification and gzip, not source size.

The chart shows today and tomorrow, true local-midnight boundaries, a zero line,
negative prices, a current-time marker and start/end/price/category inspection.
Each missing interval or missing series value breaks its path. No interpolation,
gap filling or fabricated zero is drawn. Pointer positions in gaps report no data.
Calendar arithmetic uses Date.setDate, not 86,400-second day arithmetic; timestamps
stay UTC while labels and day membership use the browser's timezone. Inspection
includes timezone names to distinguish repeated clock times at DST changes.

## Price classification: HEMS-relative, presentation only

References inspected on 2026-10-18:

- [Official Energy-Charts Strompreisampel explanation](https://www.energy-charts.info/charts/consumption_advice/chart.htm?c=DE&l=en): daily market-price 20th/80th quantiles; prices below the lower boundary or non-positive are green, prices above the upper boundary red, the rest yellow.
- [Tinkerforge batteries visualization](https://github.com/Tinkerforge/esp32-firmware/blob/63b24ce1bae2df8f123de36aab516dfc9ab9c5d2/software/web/src/modules/batteries/main.tsx#L953): ranks prices and highlights configured cheapest/most expensive quarter-hours in 20:00–20:00 windows. Also inspected day_ahead_prices, solar_forecast, eco and the shared uPlot wrappers at that revision.

Neither method is copied as an externally supplied category. HEMS needs local
calendar days, unequal/gapped normalized intervals and the effective price a user
pays, without introducing configured cheap-hour quotas. Its own documented method:

1. Split intervals at browser-local midnight. Group each piece by its local day.
2. Use effective import price when available in the series; otherwise explicitly
   use source prices. Exclude missing values rather than mix bases.
3. Sort prices ascending, weighted by covered duration. The q20/q80 value is the
   first price whose cumulative duration reaches 20%/80% of known daily duration
   (inverse empirical CDF).
4. Strictly below q20 is cheap; strictly above q80 is expensive; equality is normal.
   There is **no non-positive-price override**. A flat negative or zero-price day
   is entirely normal, while exact negative prices remain visible.
5. Partial days use available coverage and are labeled as partial. Ties can reduce
   the amount classified cheap/expensive; no arbitrary chronological tie-breaking.
   Next periods merge only contiguous pieces of the same class and never cross gaps.

The 20/80 reference comes from the published price methodology. Duration weighting,
effective-price basis and omission of the non-positive override are explicit HEMS
choices, so this is named **HEMS-relative**, not Energy-Charts' traffic light.
Classification lives only in frontend presentation helpers. Exact optimizer prices
and all economic decisions are unchanged.

## Diagnostics and configuration

All previous HEMS telemetry/quality, polling ages/counters, errors, Modbus exceptions,
constraints and EV Protection details remain on the diagnostics route. Complete
JSON snapshots retain source/fetch timestamps, source/transport age, provider
request stage, DNS/HTTP/TLS, payload/JSON/stack/heap measurements, quota/backoff and
per-plane requests, control profile identity/fingerprint, mailbox/execution/readback
and cleanup data. The optimizer keeps paginated exact schedule rows and the complete
response. System diagnostics lazily fetch the actual system-status endpoint, including
retained reset breadcrumbs, raw reset reasons, tasks and memory; the full system page
also remains linked.

Provider settings keep normalized push/HTTP bridge, timing/age/horizon and upload
support inside Advanced settings. Optimizer timing/SoC resolution is expandable;
capacity is shown in kWh and efficiencies in percent while API units stay unchanged.
Manual controls remain profile-defined semantic operations, gated by the same
`allowed`, category and EV Protection checks. No raw register write UI exists.
Successful saves still remain successful if a subsequent reload or best-effort
release produces a warning. Polling preserves unsaved form drafts; post-save
reloads are serialized with in-flight status requests.

New product messages are supplied in English, German and French through existing
locale JSON. Tests verify key and interpolation parity. Raw diagnostic identifiers
retain their diagnostic spelling. Provider message mappings use identifiers from
the current production sources, including provider_rate_limited, network_unavailable,
clock_invalid/waiting_for_time and transport/provider backoff. Unknown failures are
reported as update failures with the complete identifier in Diagnostics.

## Hardware-free validation and limitations

`yarn --cwd webapp test` runs existing Admin/auth and tariff-unit/save tests plus
presentation tests for rankings/ties, signs/zero, unequal durations, local days,
DST (Berlin/New York/Lord Howe), fractional timezone offsets, missing prices/gaps,
price formats, effective/source separation, PV daily integration, stale/unknown
signals, provider conditions and all three locale structures. The reference HEMS
CI job runs these tests.

`test/fixtures/hems.mjs` supplies deterministic API responses on a 25-hour day.
The optional `test/test_hems_browser.mjs` uses isolated Playwright and system
Chromium against Vite; it intercepts every API request and never uses hardware or
live energy providers. It covers all four routes at 360/768/1440px, light/dark,
overflow, runtime errors, pointer/touch/keyboard, rate limits, stale/gap/offline,
canonical tariff/PV saves, Monitor mode persistence and absence of device commands.
See [test instructions](../test/README.md).

Current API limits: aggregate PV only; no measured historical PV; optimizer schedules
are paginated and can be invalidated by changing inputs. The overview therefore
shows real upcoming proposals and modeled SoC endpoints instead of stitching an
apparently atomic full-horizon optimizer chart. No economic device writes were added.
Physical WARP/Marstek validation remains pending independently of this UI work.

Later UX debt: replace some raw nested diagnostic JSON with focused read-only
subsections, localize the inherited technical manual-control/profile text, and add
per-plane/atomic optimizer visualization when supported by real API data.

## Productization resource and local validation result

Same selected branch/worktree, identical backend and reference target
`generic_esp32s3_usb`; before UI is PR head `80c14071`, after is this productization.

| Resource | Before | After | Delta |
| --- | ---: | ---: | ---: |
| App JS, production minified | 1,233,698 B | 1,306,940 B | +73,242 B (+5.94%) |
| App JS, served gzip | 293,344 B | 312,072 B | +18,728 B (+6.38%) |
| index.html, served gzip | 391 B | 391 B | 0 B |
| RAM | 116,240 B | 116,240 B | +0 B |
| Flash | 2,807,221 B | 2,825,957 B | +18,736 B |
| OTA image | 2,807,584 B | 2,826,320 B | +18,736 B |

Other static assets are unchanged. No new runtime dependency, backend source,
platform, partition, worker stack, static/RTC allocation or PSRAM requirement was
introduced. Firmware image alignment can differ slightly from the embedded gzip
delta. These are local like-for-like measurements; CI branch/version strings can
produce a small absolute firmware-size difference.

Local verification passed: production `yarn build` (including type checking),
`yarn lint`, full `prettier --check src/`, `yarn test`, 31 host executables,
production HEMS configuration compatibility, semantic/no-write/TLS surface guard,
firmware-channel configuration, full repository cpplint and the reference firmware
build. Deterministic Chromium checks passed across all four routes and all three
widths, including canonical form payloads. Remote full-project CI is required on
the pushed head; its final result and exact head are reported with the PR update.
