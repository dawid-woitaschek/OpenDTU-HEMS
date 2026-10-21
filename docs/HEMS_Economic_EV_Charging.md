# Economic EV Charging: bounded shadow planning

This package is **planning only**, default disabled. It installs no EVSE producer,
dispatch loop, timer or automatic command. It never calls SetCurrent, Stop, Release,
mailboxes, actuators or Modbus. Manual commands retain the existing permission and
readiness rules. Hardware/BMS protections and EV Protection remain authoritative.

## Intent and electrical model

The additive `config.ev_shadow` object in the existing optimizer configuration is:

| Field | Meaning / bounds |
| --- | --- |
| `enabled` | Shadow planning enabled; default false |
| `deadline` | One absolute UTC Unix-second instant; enabled configurations require a synchronized-clock-era timestamp |
| `required_wh` | Remaining desired AC charging energy at each calculation, 0–1,000,000 Wh; UI shows kWh |
| `min_current_a`, `max_current_a` | Permitted whole-amp charging range, 6–80 A; minimum ≤ maximum |
| `pv_preferred` | Prefer covered residual PV; default true |
| `max_price_enabled`, `max_price_micros_per_kwh` | Optional maximum grid price in the selected exact numeric price basis |
| `nominal_voltage`, `phases` | Explicit advanced installation model; voltage 100–250 V per phase, 1–3 charging phases |

Voltage/phases default to zero (unconfigured), which blocks positive-energy plans.
There is no implicit single/three-phase assumption and no invented vehicle SoC.
The energy is AC energy at the charger, not battery-pack energy: vehicle charging
losses, ramping, availability and actual delivered energy are not known. Replanning
never deducts a previously proposed interval as physically delivered. Users must
update remaining intent when needed; metered-session accounting is deferred.

Horizon (900–172,800 seconds), base load, effective/source price basis and optional
export value come from the existing optimizer's Advanced model configuration.
They remain explicit even when storage planning is disabled. Profiles do not supply
a trustworthy charging topology, so the model does not infer it from WARP telemetry.

The browser shows departure in its local zone. Nonexistent DST times are rejected;
repeated local times require choosing a displayed offset/UTC instant. Only the
selected absolute UTC instant is saved. There are no recurring local-time rules.

## Staged residual algorithm

`Optimizer::calculateEv` consumes the same bounded normalized tariff/PV snapshots
as the storage planner, immediately after the existing storage DP completes. It
cannot change storage steps. With storage planning enabled, a valid storage
projection is required; no fictitious idle battery replaces an unavailable plan.
With storage planning disabled, the result explicitly reports the missing battery
projection and uses the configured base-load model.

1. Build at most 256 half-open UTC intervals, split at tariff boundaries, quarter
   hours, forecast boundaries (when used), storage boundaries and the exact deadline.
   Stop at the first tariff/storage gap or bounded horizon. Never extend prices.
2. Use fresh PV only if the requested bounded horizon has complete forecast coverage
   and the storage planner also used PV when enabled. Otherwise use tariff-only
   planning and explicitly report PV exclusion. Missing PV does not block tariffs.
3. Residual PV is forecast minus explicit base load and positive planned storage
   charge power. Storage-discharge intervals are excluded and reported as a conflict:
   planned battery discharge is never presented as free EV energy. Storage's existing
   plan remains authoritative even if this makes the EV goal infeasible.
4. Respect current bounds. A price cap excludes grid energy above that price; such
   slots can still use a wholly PV-covered current at or above the minimum. Deliverable
   energy is the bounded feasible capacity before selecting the requested energy.
5. Rank slots deterministically by residual-PV coverage first when PV preference is
   enabled, then exact modeled marginal price, then earliest UTC start. Fill at the
   permitted maximum current, with a partial final duration. Duration rounds upward
   by less than one second of charging energy; the API reports the actual modeled
   amount, not a fabricated exact match.
6. Cost is incremental import cost plus configured export opportunity value for PV.
   Integer watt-seconds and nano-currency avoid price-category approximations.
   AC energy, feasibility, shortfall and cost describe the model, not physical delivery.

This is a **staged residual heuristic**, not globally optimal joint-home scheduling,
and not a new multidimensional DP. PV preference can trade cost for PV use. The
model has no connection availability, phase switching, measured EV demand, grid
connection limit or validated physical command timing. It never changes the storage
schedule to satisfy EV demand. A future joint optimizer may replace it after device
validation. Incomplete coverage stays visible even if the covered portion meets the
request. A zero-energy request needs no tariff, PV or electrical model.

## Lifecycle, API and product surfaces

The existing optimizer service owns persistence and the shared energy worker. EV
intent can enable calculations independently of storage calculation. The atomic
version-1 settings file gains an optional typed object; legacy files default EV
planning off. Only intent is persisted, never schedules, simulated state or command
history. Restart recalculates against current feeds and never reconstructs ownership.

The existing authenticated `POST /api/hems/optimizer/config` accepts a complete
optimizer config as before, or `{ "ev_shadow": { ...complete EV intent... } }` alone.
The intent-only form preserves storage configuration and durable manual pause.
Invalid fields or failed persistence retain the previous configuration.

`GET /api/hems/optimizer?all=1&offset=N` adds `ev_shadow`. It exposes config, explicit
`shadow_only=true` / `device_writes=false`, reason, coverage reason, generated time,
UTC deadline, required/planned/deliverable/shortfall Wh, incremental cost, completion,
current and next proposed action, revisions, PV use and storage interaction. Schedule
pages contain at most 24 intervals including exact charging ends, currents, energy,
price, residual PV, storage power and cost. Parent `has_more` covers either schedule;
each EV page also has its own pagination flag. Inputs changed since publication
suppress the schedule and modeled totals until recalculation. Config and provenance
remain visible. Reason identifiers are deterministic and translated in en/de/fr.

Energy/config/control/storage changes invalidate both projections atomically. A
storage-plan revision is the EV plan's storage revision; unchanged input polling
neither recalculates continuously nor increments revisions. Existing five-second
minimum worker retry/replan cadence remains. Current proposed action is computed
from UTC at serialization, including a partial final charging interval.

Overview adds a compact proposal, next change, merged charging windows, energy,
completion/shortfall, modeled cost and limitations. Settings separates intent from
advanced topology. Advanced Diagnostics exposes complete EV pages. The normal
browser support snapshot merges both paginated schedules, rejects mixed revisions,
and retains normal privacy redaction without persistent device logging.

## Bounds and validation limits

No new task, HTTP endpoint, DP state dimension or persistent history is added.
`EvPlan` is 10,304 bytes on the host ABI: 256 fixed slots plus summary. It is allocated
only when enabled, after storage DP scratch is released. Sorting uses a 512-byte
index array and bounded O(N log N) work; serialization's current/next lookup is O(N), N ≤ 256. Publication retains a stable EV allocation and one candidate (about
20.6 kB EV peak on the host ABI); it never copies tariff/PV series into persistent
EV state. An EV allocation failure reports `insufficient_memory` while publishing
the independently computed storage result. No additional schedule allocation exists
while EV planning is off. ESP32 ABI size, largest free block, timing and stack impact
still require physical no-PSRAM ESP32-S3 measurement; host bounds are not that evidence.

Focused native tests execute the real planner, service, settings store, policy,
controller, providers, mailbox and semantic executor through deterministic clock,
worker and transport seams. The [test guide](../test/README.md) describes individual
scenarios and semantic JSON logs. Browser tests cover presentation/request payloads
with deterministic API fixtures; they do not run firmware in the browser.

WARP3, Marstek, EV Protection and Economic Storage Dispatch **physical validation
remains pending**. No host scenario emulates a physical Modbus device or establishes
real charge/discharge, register mapping accuracy or watchdog behavior. Automatic
economic EVSE dispatch is **not implemented**. Follow the separate
[hardware runbook](HEMS_Hardware_Validation.md).

This feature branch starts at PR #32 head `59e5b22d` and targets
`feature/hems-validation-readiness` while #32 remains open. After #32 merges, rebase
its feature commits onto `hems` (or retarget directly if ancestry is retained).
PR #31 (`feature/cmt-radio-rework`) is separate/open/unmerged as inspected on
2026-10-22; no radio files, commits, APIs or behavior are used here.
