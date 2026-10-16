# Dynamic tariff and optional PV forecast

HEMS Settings now contains compact tariff and forecast cards: provider settings,
current normalized data, future intervals, freshness, horizon coverage and update
errors. Both feeds are disabled by default and independent of HEMS device-control
permissions. Neither feed creates economic device commands. Disabled, missing or
stale forecast/tariff data never prevents the existing core or EV Protection from
operating.

No commercial provider was selected in the repository. The first adapters are
**normalized JSON push** and **normalized HTTP polling** from a local bridge.
A Home Assistant integration, gateway or other external process can convert its
chosen vendor's payload into this versioned contract. Vendor credentials, taxes,
fees and timezone conversion belong at that boundary; a wholesale source price
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
| Either route with `?all=1` | GET | Include all retained intervals (at most 128) |
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

`valid` means a structurally validated series is retained, even when stale.
Use `fresh` separately before consuming it. Source `generated_at` and the local
successful `fetched_at` are both visible. Freshness uses source age and monotonic
time since receipt, so fetching an old cached response does not make it fresh.
An unsynchronized/backward clock or source timestamp more than 5 minutes in the
future is not usable. Older source revisions cannot replace a newer series.

`horizon_complete` means continuous coverage from now through the configured
requested horizon, independently of freshness. `future_count` includes the current
interval when it has not ended. `current` is null in a gap. `state` distinguishes
Disabled, waiting for time/data, Ready, Partial, Stale and update failure.
`last_error` is a stable diagnostic code; it clears on a successful update.
A failed HTTP/network/parse update retains previous data; it may still be fresh
until its maximum age expires. Consumers must inspect errors and freshness,
not just whether a series exists. The UI explicitly shows both.

Polling retries at the configured interval; Refresh now can request an earlier
retry. Requests are coalesced, with at most one worker shared by both feeds. Slow
network calls never execute in the policy/DPL scheduler or Modbus workers. No
HTTP worker is allocated for disabled or push-only configurations; once created,
it remains idle when no HTTP feeds are enabled.

## Limits and optimizer use

Each domain holds at most 128 intervals, each at most 24 hours long. This is
32 hours at 15-minute resolution (including a 25-hour DST day), or about five
days hourly. Longer requested horizons simply remain Partial; extra intervals
are rejected, never silently truncated. Payload maximum is 20,000 bytes and the
JSON allocator is capped at 24,576 bytes. A series is 2620 bytes on the native
host; two fixed buffers plus settings add roughly 6 kB of static state. The
optional worker uses a 4096-byte stack. Parsing candidates are heap allocated;
existing task stacks are unchanged. No PSRAM dependency or new HTTP library.
Measure final ESP32 RAM/flash in the authoritative CI build; native allocation
figures are not on-device heap measurements.

`Energy::Provider` supplies the normalized series, freshness/status and refresh
eligibility. `Service::snapshot` copies one consistent bounded domain snapshot
under its mutex; callers should allocate the series away from small task stacks.
`Series::current`, `covers` and (for forecasts) `energyWattSeconds` need no vendor
payloads. Existing normalized grid/PV/storage/EVSE state, profile capabilities,
permissions and EV Protection constraints remain available through the controller.
A later optimizer must check all input freshness and know storage capacity,
reserves and efficiencies explicitly before planning. There is no invented
strategy, inferred efficiency, threshold or second actuation pipeline here.

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
water and LittleFS power-loss behavior. Commercial vendor adapters and upstream
DST conversion need provider-specific fixtures when a service is selected.
Actual WARP Modbus/IEC states, Marstek V148 sign/scales/watchdog/release and
physical EV Protection remain hardware-unverified; these feeds do not change
those mappings or gates. Economic strategy, TLS/cloud adapters, translations and
release polish remain deliberate later work.
