# Native energy providers and economic Shadow Optimizer

Software pass: 2026-10-17, Draft PR #23, `feature/hems-real-control` →
`integration/hems-next`. Device control, permissions and EV Protection retain the
existing semantic command path. **Economic planning has no device-write path.**

## Setup

Open **HEMS → Settings**. The native providers need the controller's working
network connection and synchronized UTC clock; no external bridge is required.

1. In Dynamic electricity tariff, enable **Energy-Charts · DE-LU**. This public
   Day-Ahead source needs no token. Configure your own **net** variable grid
   fees/taxes, supplier markup and VAT, and explicitly enable the variable-price
   calculation. The firmware does not invent tariff components.
2. In PV forecast, select **Forecast.Solar** and add up to **six** independently
   enabled planes. Enter each plane's decimal latitude/longitude, tilt 0..90°,
   azimuth −180..180° (south 0°, east −90°, west +90°), and installed kWp. Empty
   coordinates are rejected. Each plane has its own request and diagnostics.
3. Configure the Shadow Optimizer: capacity Wh, explicit SoC reserve bounds,
   charge/discharge efficiencies (per mille: 950 = 95%), AC power limits, constant
   expected base load, horizon and price basis. Acknowledge the SoC bounds.
   Capacity/efficiencies/power limits of zero produce explicit Not-ready blockers.
4. Enable shadow calculations. Fresh reachable normalized storage SoC is required.
   Monitor permission is sufficient for observation; no device ownership or write
   permission is acquired. A valid tariff is required; PV is optional.

Native source selection supplies visible polling defaults: Energy-Charts one
hour; Forecast.Solar two hours and a three-hour forecast age limit. Values remain
editable subject to native minimum polling intervals. No Tibber credentials or
adapter are added in this pass.

## Sources and reference review

The public [Energy-Charts API and OpenAPI contract](https://api.energy-charts.info/)
were inspected on 2026-10-17, including its current `/price?bzn=DE-LU` response:
`unix_seconds`, nullable `price`, `unit`, and `license_info`. The v1 endpoint remains
supported by the current v2 documentation and supplies UTC interval starts, avoiding
local-time ambiguity. Germany/Luxembourg is the deliberate bounded region in V1.
The provider queries the complete German local today and tomorrow, using CET/CEST
calendar boundaries (23/25-hour days). It handles hourly and quarter-hour series,
negative values, unpublished/null prices and gaps. An interval's duration is the
smallest supported observed resolution; gaps do not stretch a price across missing
periods. Unpublished tomorrow means a partial horizon, not fabricated prices.
Prices in EUR/MWh become integer micro EUR/kWh by multiplying by 1000 and rounding
once at the adapter boundary. Changing/unknown units fail the update.

Attribution: **Energy-Charts.info; Bundesnetzagentur / SMARD.de; CC BY 4.0**.
The API documentation explicitly permits public access and specifies attribution,
rate limiting and commercial access terms. Commercial redistribution/operators
must review those terms. This firmware does not use Tinkerforge's backend.

[Forecast.Solar's official public API description](https://forecast.solar/) provides
today/tomorrow hourly forecasts without an API key. Its linked
[Swagger contract](https://api.forecast.solar/swagger.yaml) and
[estimate documentation](https://doc.forecast.solar/api:estimate) are the reference
links. The documentation host was blocked by the research service's robots policy
and the workspace's outbound policy; the browser research tool could not decode
the Swagger YAML. The official homepage's request/response contract and current
public clients informed the adapter; **live `time=utc&full=1` response compatibility
still needs a real-provider smoke test**. This limitation is separate from tested
software parsing. No live provider requests run in CI.

The current Tinkerforge reference was inspected at
[`63b24ce1bae2df8f123de36aab516dfc9ab9c5d2`](https://github.com/Tinkerforge/esp32-firmware/tree/63b24ce1bae2df8f123de36aab516dfc9ab9c5d2/software/src/modules):
`solar_forecast`, `day_ahead_prices`, and verified HTTPS concepts. Independent
requests/planes, scheduling, retries and bounded horizons inform this implementation;
no module source or visual design is copied. Inherited OnBattery/upstream network
code was reviewed at OnBattery `b3923d10cbb5feb6cba21813aa0f77bc553a1236`
and OpenDTU `8da432d52f33a7598deb93550df2115b8e5bb700`; MQTT has configurable CA verification, but there is no reusable
verified provider HTTP transport. The existing generic `HttpGetter` remains unchanged.

## Verified HTTPS

`hems/energy/Transport.cpp` is used only for the two fixed native HTTPS origins.
It installs public CA trust anchors, calls `HTTPClient::begin(client, DNS_URL)`
(preserving the DNS hostname for SNI and certificate SAN verification), and disables
redirects. There is no IP-address substitution, insecure fallback, custom origin,
certificate-error checkbox or provider credential. TLS chain, validity time and
hostname validation are performed by Arduino-ESP32's mbedTLS-backed
`WiFiClientSecure`; diagnostic failures retain previously accepted data.

`Trust.h` contains public Mozilla/system-store roots: ISRG X1 (2035), X2 (2040),
DigiCert Global Root G2 (2038), and Google Trust Services R1..R4 (2036). These roots
are firmware-owned and need maintenance if provider trust chains change. They are
CA anchors, not pinned leaf certificates. Root expiry does not establish that a
provider's current chain is compatible; live TLS smoke testing is still required.
The firmware must fail closed when its clock or trust anchors are unsuitable.

HTTP connect/body timeouts are 5 seconds, TLS handshake timeout 10 seconds, and
body parsing has a 15-second deadline. HTTP 429/503 honors numeric Retry-After
with a conservative floor; date-form Retry-After falls back to that floor.
Responses are limited to 40,000 bytes and JSON allocation to 49,152 bytes. Filtering
removes unrelated provider fields before allocation. Truncated Content-Length,
trailing non-JSON data, unsupported origins, redirects, malformed/oversized JSON,
unknown units and capacity excess reject the update. No response bodies, URL
credentials or tokens are logged or returned in status.

## Forecast normalization and scheduling

The public `/estimate/lat/lon/tilt/azimuth/kWp?time=utc&full=1` adapter consumes
`watt_hours_period` as energy **ending** at each UTC key, not instantaneous power.
The initial zero is a boundary, not an interval. Subsequent period Wh become
rounded average integer W over the preceding period. Night zero is valid; a missing
period/gap over one hour is rejected, never replaced with assumed sunshine or zero.
Explicit UTC requests avoid DST/local-time conversion guesses. ISO timestamps with
explicit offsets are converted without changing the process timezone.

All enabled planes must succeed for an aggregate refresh to replace retained site
data. Aggregation sums power over the union of boundaries within the common
covered horizon, timestamps the result with the oldest plane source timestamp,
and rejects gaps, overflow or more than 256 resulting intervals. No vendor JSON
reaches the optimizer. Per-plane fetch/error/rate information is visible.
`message.info.time_utc` is the provider response timestamp; this is not a claim to
know the underlying weather-model generation time. The tariff v1 API exposes no
source-generation timestamp: `source_timestamp_known=false`, `generated_at=null`.

Forecast native requests are at least two hours apart per full refresh, with
five seconds between enabled planes. Six planes therefore use at most six requests
per normal two-hour cycle. Rate exhaustion stops remaining requests and retains
the old aggregate. Native errors set a 10-minute through 5h20 backoff, respecting a
larger Retry-After; normal native polling floors also still apply. Native Refresh now cannot bypass polling/backoff floors.
Energy-Charts polls at least hourly. One optional low-priority worker services
both feeds and optimization; HTTP and DP do not run in the scheduler/Modbus workers.
Configuration generations discard late responses. No PSRAM is needed.
The optional energy worker grows from 4096 to **8192 bytes**: ESP32 compiler
measurements showed 608 bytes in workerEntry + 2128 in fetch + 352 in the verified
transport before HTTP/TLS call depth, leaving insufficient headroom at 4096.
No TCP/IP, AsyncTCP, Modbus or main-loop stack is changed. Energy status exposes
worker_stack_bytes and worker_stack_free_bytes (ESP-IDF high-water bytes). This
reports real on-device headroom after a request/calculation; native stub values
are not hardware measurements.

## Normalized data and freshness

The v1 JSON schemas and monetary/sign/timestamp conventions remain compatible.
Capacity increases **128 → 256 intervals**, i.e. **64 hours at 15-minute resolution**,
including 48 hours plus DST. Interval 257 is rejected; there is no silent truncation.
The optional local HTTP and authenticated normalized JSON push paths remain.
Settings use the existing atomic `/hems-energy.json` persistence contract.

Status separates structural `valid`, source timestamp/age (when known), successful
`fetched_at`, monotonic transport age, current coverage, complete requested horizon,
partial horizon, source error and `planning_usable`. Backward/unsynchronized clocks
and excessively future source timestamps are unusable. Older known source revisions
cannot replace newer data. Failed refreshes preserve working series and surface a
separate error.

Tariff source age is provenance, **not interval expiry**. Yesterday's prices remain
usable during authoritative covered intervals despite an old source timestamp or
failed refresh. Forecast usability additionally requires both source/transport age
within the configured limit. `planning_usable` means fresh usable current coverage;
the planner checks its own contiguous future slots and reports Partial when the
requested tariff horizon is short. Missing/stale PV is excluded from the whole plan.
UI daily totals refuse to bridge unknown forecast gaps; price averages explicitly
say “covered average” and use duration weighting. UI calendar summaries use the
browser timezone, while tariff fetching uses Germany's calendar.

## Effective import price

All additions require explicit configuration. With VAT in basis points:

```
effective_micro_per_kWh = round((market_micro_per_kWh + grid_micro_per_kWh
                                + markup_micro_per_kWh) * (10000 + VAT_bp) / 10000)
```

Signed int64 intermediates and range checks preserve negative prices and prevent
int32 overflow. A configured variable calculation deliberately replaces a supplied
normalized import price; leave it disabled to use an authoritative bridge import
price. **The monthly base fee is reporting-only** and never enters marginal battery
cost. Currency conversion and tax accounting are outside V1.

## Economic model

The production `Optimizer::calculate` performs bounded dynamic programming over
UTC time slots × battery energy states. It minimizes net modeled import cost minus
explicit export revenue, with a secondary minimum-throughput objective for ties.
There are no charge/discharge price thresholds. Each transition enforces reserve
bounds, AC powers, interval duration and separate efficiencies. Initial SoC is fresh
normalized telemetry, never inferred. Final stored energy must be at least initial
stored energy; there is no unpriced depletion of the initial battery stock.

The load model is the configured **constant future base load**, minus fresh PV when
available. Without explicit export compensation, export revenue is zero and battery
discharge cannot deliberately create export. The first/current slot applies current
storage/EV Protection constraints. Future EV state is unknown, not predicted. The
same model computes baseline (battery holds) and optimized cost.

Price basis is explicit: effective variable import (preferred) or market/source.
Missing required import price blocks rather than silently falling back. Monetary
objective uses signed integer **nano currency** (cost_nano), derived from integer
watt-seconds and canonical micro currency/kWh. No floating-point money enters DP.
Forecast averaging and battery discretization have explicit finite precision.

Horizon: 15 minutes..48 hours; up to 256 slots, split at UTC quarter-hours and tariff
boundaries. A short/gapped tariff stops the plan with Partial, never fills prices.
Precision: 2..10 percentage-point energy spacing (default 5), plus exact measured
initial Wh and configured endpoints. At most 52 distinct states, 53 allocated.
Actions below the chosen energy step may be unreachable; select finer precision
for small batteries/power limits. Wh rounding changes initial energy by at most
half a Wh. No degradation cost, demand-charge model, household load forecast,
future EV forecast or terminal resale value is invented. Negative-price loss
cycling can be economically beneficial in this simplified objective; review that
limitation before considering Active optimization.

DP parent history is a heap-owned 256×53 byte array with two rolling score rows;
workspace ≈15.5 kB. Plan is approximately 16.5 kB on the native host. No matrix lives on a
loop/task stack. Snapshots/results are bounded, allocation failure is diagnostic,
recalculation coalesces, and changed config/energy/storage/policy inputs discard
late results. Stale results suppress schedule rows until recalculated. Current
plans include a paginated explanation of power, SoC, PV/load, prices, grid energy,
interval cost, blockers, assumptions, timestamps and modeled savings.

## Authenticated APIs

Existing energy routes remain (see [normalized contract](HEMS_Energy_Data.md)).
`config.provider`: `energy_charts` for tariff, `forecast_solar` for PV, or
`normalized_push` / `normalized_http`. Both domains support `enabled=false`.
Plane coordinates are integer `latitude_e6` / `longitude_e6`; UI converts degrees.
Pricing fields: enabled, vat_basis_points, grid_micros_per_kwh,
markup_micros_per_kwh, monthly_micros. Plane count max 6; config body max 8192 bytes.

| Route | Method | Result |
|---|---|---|
| `/api/hems/optimizer` | GET | Config, readiness, summary and up to 12 schedule rows |
| `/api/hems/optimizer?all=1&offset=24` | GET | Up to 24 rows from offset; has_more and schedule_offset |
| `/api/hems/optimizer/config` | POST | Atomically persist pure-planning settings in `/hems-optimizer.json` |
| `/api/hems/optimizer/recalculate` | POST | Coalesce a calculation request; 202, or 409 when disabled |

POST uses the existing authenticated `data` form field. Config max 2048 bytes;
malformed/incomplete/out-of-range settings are rejected. API config names are
shown directly in `serializeConfig` and the UI: enabled, soc_bounds_configured,
capacity_wh, min_soc_pct, max_soc_pct, max_charge_w, max_discharge_w,
charge_permille, discharge_permille, expected_base_load_w, horizon_seconds,
soc_step_pct, effective_price, export_configured, export_micros_per_kwh.
There is **no optimizer device-command endpoint**. The core links without any
controller. The service integration test links against a controller exposing only
read-only snapshots. Architectural surface tests forbid semantic write imports/calls.

## Validation and remaining work

Deterministic native tests run production normalization, provider service,
transport orchestration, planner and optimizer service with mocked network/time/FS
boundaries. Tests include native provider success/failure/recovery, fixed TLS
origins/CA wiring/no redirects, body bounds/truncation, rate retry, generation
changes during requests, old tariff source/current coverage, stale PV source,
negative prices, 192/196/256/257 intervals, DST short/long days, plane validation,
aggregation, pricing/VAT/overflow, optimizer losses/reserves/powers/export,
EV Protection, price-basis blockers, determinism, 48-hour DP, extreme integer costs,
zero-benefit hold and normalized tariff+PV+storage → economic plan.

These are software tests, not cryptographic handshake or physical-device proofs.
Real internet TLS/DNS, current Forecast.Solar full UTC shape, six-plane quota
behavior, ESP32 optimizer timing/heap/stack high-water and network recovery need
on-device validation. WARP3 physical writes remain unvalidated. Marstek V148/V119
physical signs/scales/acquire/release/watchdog remain unvalidated. EV Protection
physical end-to-end timing and override/recovery remain unvalidated.

Before any Active economic optimizer: validate those device behaviors and energy
providers, refine V1 modeling/precision as required, define dispatch/replanning
and manual override semantics, and use the existing permission-gated
provider → mailbox/worker → planner → executor path. This pass implements none of
those economic writes.
