# Tariff presentation and provider status

Tariff display levels are frontend-only. The optimizer continues to consume
exact signed integer tariff/import prices (`usedPrice` in the planner); no
classification enters optimization, scheduling, storage or EVSE control.

## Relative daily price levels

Intervals are split at browser-local calendar midnights using calendar arithmetic,
including CET/CEST 23/25-hour days. Null/missing prices remain gaps. For each day's
available intervals, duration-weighted p10 and p90 establish a robust price span.
A span below **2 ct/kWh** (20,000 millionths of EUR/kWh) makes every interval normal.
Non-EUR feeds use the same **0.02 currency units/kWh** display noise floor; no
currency conversion is implied. This absolute consumer-unit floor also suppresses
sub-cent fluctuations around zero and negative prices.

For a meaningful span, the low threshold is p10 + span/3 and the high threshold
is p90 - span/3. Strictly lower/higher values are Low/High; ties are Normal.
No prices are smoothed or fabricated. One short spike cannot dominate p10/p90;
a flat robust range remains normal even with an isolated outlier. Partial days
use only accepted available data. A next-period summary stops at a gap, null price
or different class. Adjacent 35.16 and 35.69 ct/kWh prices can share the low band
on a day with a wider robust range.

The chart derives its Y range from visible primary values and, when shown, source
prices. It adds 12% padding, with readable 1/2/5 ticks. Zero appears when within
the data or padding; ordinary 30–50 ct/kWh data no longer forces a zero baseline.
Flat/empty data gets a finite nonzero range. PV power explicitly keeps its zero
baseline. Underlying values remain unchanged.

English Low/Normal/High, German Niedrig/Normal/Hoch, and French Bas/Normal/Élevé
are neutral display terms. All three locale explanations describe the same rule.

## Availability, freshness and requests

Display levels and control semantics remain independent of provider parsing. Tariff planning usability
follows authoritative current coverage even when source or transport age exceeds
the configured maximum. Forecast usability still requires both ages within that
maximum and current coverage. A partial future horizon does not imply a missing
current interval. A failed refresh retains previously accepted data.

The API additionally exposes `source_fresh` (null when its timestamp is unknown)
and `transport_fresh`. They report age/clock checks independently of tariff
planning usability, alongside the existing source/transport ages. There are no
new workers, payload buffers, JSON caps, or retained provider documents.

The tariff badge says current prices are covered rather than claiming the source
was recently updated. Details and diagnostics show source freshness and last
successful receipt separately. Transport failures and rate limits remain refresh
warnings alongside retained-data/availability status. A rejected manual refresh
(busy, quota or backoff) does not hide usable values or mark data unverified.
Failure to retrieve device API status separately makes retained data unverified.
The generic transport-backoff wording also covers normal anti-hammer spacing
after a successful request; it no longer falsely claims that connection failed.

## Verification boundary

Deterministic frontend tests cover daily weighted bands, absolute noise, outliers,
negative prices, nulls, partial days, alternate resolutions, local/DST grouping,
true contiguous periods, axis ranges, provider status, action rejection and
locale parity. Architectural guards and the existing native category-invariance
fixture protect numeric-price optimizer inputs. Native provider/runtime tests
exercise retained data, independent ages, current/partial coverage, fixed HTTPS
origins, CA/hostname wiring, redirects disabled, bounded parsing, hourly/two-hour
ordinary cadence, serial requests with five-second plane spacing, backoff/quota
holds and generation changes.

Official provider specifications and bounded external HTTP 200 captures supplied
on 2026-10-19 supersede the earlier environment-access limitation. Energy-Charts
uses strict `/v2/price` parsing with provider response-generation time distinct
from fetch time and interval coverage. Forecast.Solar's public contract is
verified; exact East `full=1` and sparse East/West requests succeed externally.
The specific `full=1` behavior remains undocumented in the supplied Swagger.
See [the modernization audit](Modernization_Audit.md) for evidence and limits.
This work performs no physical board access.

Physical checks remain: `generic_esp32s3_usb` on the no-PSRAM ESP32-S3/8 MB board,
native TLS heap/stack under multi-plane load and network recovery, flashed UI
metadata/status rendering, WARP3 writes/EV Protection timing, and Marstek register
signs/scales/acquire/release/watchdog/manual precedence. Software fixtures and
browser screenshots do not establish those physical behaviors.
