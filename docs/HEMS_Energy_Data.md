# Dynamic tariff and optional PV forecast

Native Forecast.Solar, Energy-Charts pricing, verified HTTPS and the economic
Shadow Optimizer are documented in [Native energy and Shadow planning](HEMS_Native_Energy.md).
The normalized push/local HTTP contract below remains available.


HEMS Settings now contains compact tariff and forecast cards: provider settings,
current normalized data, future intervals, freshness, horizon coverage and update
errors. Both feeds are disabled by default and independent of HEMS device-control
permissions. Neither feed creates economic device commands. Disabled, missing or
stale forecast/tariff data never prevents the existing core or EV Protection from
operating.

In addition to the native providers, alternative adapters are
**normalized JSON push** and **normalized HTTP polling** from a local bridge.
A Home Assistant integration, gateway or other external process can convert its
chosen vendor's payload into this versioned contract. Vendor credentials and timezone conversion belong at that boundary. Explicit
variable fees/VAT can now be configured locally; a wholesale source price
is never silently treated as the end customer's import price.

## Canonical contract

All times are **UTC Unix seconds**, not milliseconds or local date strings.
Intervals are half-open `[start,end)` and must be ordered and non-overlapping.
Duplicate, overlapping, reverse or zero-length intervals reject the entire
update. Missing intervals are retained as gaps, never interpolated. The bridge
must convert local/DST timestamps to UTC: repeated local hours have different UTC
starts. Native tests exercise 92/100 quarter-hour intervals (23/25-hour days).
The UI displays the browser's timezone and offset, including DST.

Tariff (`opendtu-hems-tariff/v1`):

```json
{
  "schema": "opendtu-hems-tariff/v1",
  "source": "my-local-tariff-bridge",
  "generated_at": 1792180800,
  "currency": "EUR",
  "intervals": [
    {"start": 1792180800, "end": 1792181700,
     "price_micros_per_kwh": -50000, "import_price_micros_per_kwh": 100000},
    {"start": 1792181700, "end": 1792182600,
     "price_micros_per_kwh": 250000}
  ]
}
```

Prices are signed **integer millionths of currency per kWh**. For EUR, `250000`
means EUR 0.25/kWh = 25 ct/kWh; `-50000` means EUR -0.05/kWh. The source price is
required; the effective/import price is optional (omitted/null means unknown).
The three-letter uppercase currency applies to the whole series. Decimal/string
prices, booleans and values outside int32 are rejected. No currency conversion,
tax/fee calculation or economic thresholds are implied.

Forecast (`opendtu-hems-forecast/v1`):

```json
{
  "schema": "opendtu-hems-forecast/v1",
  "source": "my-local-pv-bridge",
  "generated_at": 1792180800,
  "intervals": [
    {"start": 1792180800, "end": 1792181700, "power_w": 2000},
    {"start": 1792181700, "end": 1792182600, "power_w": 1000}
  ]
}
```

`power_w` is nonnegative integer **average watts** over the interval. The API
also exposes `energy_wh = power_w * duration_seconds / 3600`. The first example
interval is 500 Wh, not 2000 Wh. The normalized model can aggregate arbitrary
subranges as exact integer watt-seconds; aggregation fails over gaps. Zero at
night is valid; an absent interval is unknown. Bridges supplying interval Wh
must explicitly convert it to average W. Fractional W are currently unsupported.

Example timestamps above demonstrate the format: replace them and the values
with current real provider data before uploading. They are not live prices or
predictions.

## Configuration and API

Authenticated routes use the existing Web API credentials and `data` form field:

| Route | Method | Behavior |
| --- | --- | --- |
| `/api/hems/energy/tariff` | GET | Status, settings, current interval and next 8 intervals |
| `/api/hems/energy/forecast` | GET | Same contract for PV forecast |
| Either route with `?all=1` | GET | Include all retained intervals (at most 256) |
| Either route | POST | Ingest normalized JSON in push mode |
| Either route + `/config` | POST | Persist provider settings |
| Either route + `/refresh` | POST | Queue a refresh for an enabled HTTP provider (202); otherwise 409 |

Configuration for either domain:

```json
{
  "enabled": true,
  "url": "http://bridge.local/tariff.json",
  "refresh_seconds": 900,
  "max_age_seconds": 3600,
  "horizon_seconds": 86400
}
```

Leave `url` empty for push mode and use the UI JSON upload or authenticated POST.
For example, with curl prompting for the administrator password:

```sh
curl --user admin --form-string 'data={"enabled":true,"url":"","refresh_seconds":900,"max_age_seconds":3600,"horizon_seconds":86400}' \
  http://DTU/api/hems/energy/tariff/config
curl --user admin --form 'data=<tariff.json' http://DTU/api/hems/energy/tariff
curl --user admin 'http://DTU/api/hems/energy/tariff?all=1'
```

The HTTP bridge must return a 200 response with the same JSON object, using a
known Content-Length or HTTP/1.0 connection close (existing `HttpGetter` uses
HTTP/1.0). A 3-second HTTP timeout, payload limit and JSON allocation cap apply.
Only `http://` local-bridge URLs are supported in this first adapter; embedded
credentials and fragments are rejected. Use a trusted LAN bridge for upstream
HTTPS/authentication; no new insecure TLS mode or vendor-token storage is added.
Refresh interval is 60..86400 seconds, maximum age 60..604800 seconds, requested
horizon 900..604800 seconds. URL maximum is 192 bytes.

Settings are atomically stored in `/hems-energy.json` (both domains, schema
version 1). Failed writes retain runtime settings. Data buffers are RAM-only;
a reboot waits for the next successful fetch/upload. Changing the endpoint or
enabled state clears the old series. An in-flight response from an earlier
configuration generation is discarded. Settings never grant device writes.

## Freshness, validity and failure

`valid` means structurally validated retained data. `fetched_at` is the last
successful receipt; `transport_age_seconds` and `source_age_seconds` are separate.
Tariff usability follows authoritative current/future interval coverage, regardless
of source age. Forecast freshness requires source/transport age within max_age_seconds.
Unknown native tariff generation time is exposed explicitly rather than invented.
An unsynchronized/backward clock or source timestamp more than five minutes ahead
is unusable. Failed refreshes retain data; see the native energy document for details.

`horizon_complete` means continuous coverage from now through the configured
requested horizon, independently of freshness. `future_count` includes the current
interval when it has not ended. `current` is null in a gap. `state` distinguishes
Disabled, waiting for time/data, Ready, Partial, Stale and update failure.
`last_error` is a stable diagnostic code; it clears on a successful update.
A failed HTTP/network/parse update retains previous data; it may remain usable for covered tariff intervals or, for PV,
until its maximum source/transport age expires. Consumers must inspect errors and freshness,
not just whether a series exists. The UI explicitly shows both.

Polling retries at the configured interval; Refresh now can request an earlier
retry. Requests are coalesced, with at most one worker shared by both feeds. Slow
network calls never execute in the policy/DPL scheduler or Modbus workers. No
HTTP worker is allocated for disabled or push-only configurations; once created,
it remains idle when no HTTP feeds are enabled.

## Limits and optimizer use

Each domain holds at most 256 intervals, each at most 24 hours long. This is
64 hours at 15-minute resolution (including a 25-hour DST day), or about ten
days hourly. Longer requested horizons simply remain Partial; extra intervals
are rejected, never silently truncated. Payload maximum is 40,000 bytes and the
JSON allocator is capped at 49,152 bytes. A series is 5184 bytes on the native
host; two fixed buffers plus settings add roughly 12 kB of static state. The
optional shared provider/optimizer worker uses an 8192-byte stack (measured compiler
frames justified additional TLS headroom; other task stacks are unchanged). Parsing candidates are heap allocated;
non-energy task stacks are unchanged. No PSRAM dependency or new HTTP library.
Measure final ESP32 RAM/flash in the authoritative CI build; native allocation
figures are not on-device heap measurements.

`Energy::Provider` supplies the normalized series, freshness/status and refresh
eligibility. `Service::snapshot` copies one consistent bounded domain snapshot
under its mutex; callers should allocate the series away from small task stacks.
`Series::current`, `covers` and (for forecasts) `energyWattSeconds` need no vendor
payloads. Existing normalized grid/PV/storage/EVSE state, profile capabilities,
permissions and EV Protection constraints remain available through the controller.
The implemented Shadow Optimizer checks freshness, contiguous coverage, capacity,
explicit reserves and efficiencies before planning. It shares this normalized
contract and has no actuation pipeline. See [native energy and planning](HEMS_Native_Energy.md).

## Validation level

Software validated with the existing native Makefile/ArduinoJson framework:
parsing and bounds, integer/negative prices, optional import cost, half-open UTC
lookup, gaps/overlap/duplicates, 23/25-hour days, W/Wh/subrange aggregation,
source and monotonic age, disabled/stale states, failure/recovery, retry timing,
settings round-trip/reload, rejected persistence, shared-worker behavior and
configuration-generation cancellation. Runtime tests replace only network,
LittleFS, clock and task boundaries; production service/provider code executes.
UI tests/type/build checks cover save response semantics and rendering.

Hardware validation pending: real ESP32 network timing, worker-stack/heap high
water and LittleFS power-loss behavior. Native-provider/DST fixtures are now included; real provider TLS and response-shape
smoke tests remain pending.
Actual WARP Modbus/IEC states, Marstek V148 sign/scales/watchdog/release and
physical EV Protection remain hardware-unverified; these feeds do not change
those mappings or gates. Economic shadow calculation and verified native transport are implemented; Active
economic dispatch, translations and release polish remain later work.
