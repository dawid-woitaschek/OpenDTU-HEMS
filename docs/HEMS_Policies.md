# Storage/economic policies

User configuration → policy evaluation (constraints/goals) → existing economic DP
→ read-only plan. The policy model and DP perform **zero device writes**. Separate
[opt-in economic dispatch](HEMS_Economic_Dispatch.md) implements runtime control. This is
a small explicit battery policy model, without rule tables, scripting or price
category triggers. Cheap/normal/expensive remain frontend presentation only.

## Implemented semantics

| Setting | Exact meaning |
| --- | --- |
| Grid charging | When forbidden, a charging transition may use only modeled PV surplus after the configured base load. It may not increase grid import for the battery. This is separate from the device write permission. |
| Battery reserve | Existing `min_soc_pct`: lower planning bound at every state, rounded up to whole Wh. This is a user planning reserve, not a BMS/hardware safety setting. |
| Maximum desired SoC | Existing `max_soc_pct`: upper planning bound, rounded down to Wh. Hardware limits remain authoritative. |
| Target charge | Optional minimum SoC at one absolute UTC deadline. The deadline splits a DP slot when necessary; the target Wh is an exact additional energy state. It is enforced at the deadline, then the ordinary reserve applies. |
| Economic discharge | When forbidden, all negative battery-energy transitions are rejected, including compensated export. It does not disable EV Protection or manual semantic actions. |

The optimizer continues minimizing exact per-slot modeled cost with separate
charge/discharge efficiencies, AC power limits, base load, optional usable PV and
the existing minimum-throughput tie breaker. Grid charging being allowed does not
require it. A relatively normal price may fund later expensive avoided import;
a cheap price need not cause charging. Without explicit export compensation,
battery discharge cannot intentionally create export. Final energy remains at
least the measured initial energy, independently of a target reached earlier.

An initial SoC outside the planning bounds retains the existing explicit blocker;
this milestone does not silently relax the user's reserve or upper bound. Power
limits remain explicitly configured assumptions. Actual device capabilities,
readiness and protection must be rechecked before any eventual actuation.

## Deadline, coverage and precision

The UI accepts a **date and local time**, shows the browser timezone and displays
the saved instant with a timezone label. Firmware persists UTC seconds. The target
does not recur or roll into tomorrow. After its deadline, planning blocks until
the target is disabled or dated again. Browser time conversion rejects invalid
dates and nonexistent DST times; ambiguous edited fall-back times follow the
browser Date convention (the earlier occurrence). An already saved UTC instant
is retained unless the time input is edited.

The target must be within the planning SoC bounds. If the trustworthy contiguous
tariff horizon or configured planning horizon ends before the deadline, the plan
is explicitly Partial and the target is **not evaluated/enforced**. No prices,
future slots, PV energy or target certainty are invented. A visible warning names
that limitation even if another partial-horizon warning is also present.

A covered target with no feasible DP path returns `target_unreachable` and no
schedule. This includes insufficient time/power, forbidden grid charging without
sufficient PV, current protection constraints and discretization limitations.
The blocker does not claim physical impossibility beyond this model. PV is
included only if fresh/usable and covering the entire computed plan; otherwise
the existing whole-plan PV exclusion and warnings apply.

The existing 2–10 percentage-point grid remains, plus bounds, exact initial Wh
and target Wh. Maximum allocation grows by one energy state, without increasing
the 256-slot limit. Targets may add a slot boundary. Fine actions can still be
unreachable at coarse resolution. No hidden precision or arbitrary saving
threshold is added.

## Costs and explanations

The baseline still holds the battery at its initial energy. It is a transparent
comparison, not a second controller or necessarily a target-feasible schedule.
If initial SoC is below the target, a warning states that the hold baseline does
not meet it. Meeting user intent can have **negative modeled savings**, shown
without clamping or relabeling as a benefit. All figures refer to the displayed
planning horizon and remain estimates.

Settings/overview expose persisted policies, target/deadline, current protection
restrictions, readiness/blockers, modeled cost/benefit and no-device-writes status.
Schedule reasons describe membership in the least-cost feasible horizon plan;
they are not invented per-action counterfactual proofs. Permission restrictions
explain why price alone cannot authorize an action.

Minimum-benefit hysteresis and recurring schedules are deliberately deferred.
Efficiency losses and minimum-throughput tie breaking already remain modeled,
but tiny positive modeled benefits are not filtered by a new threshold. A future
threshold must distinguish optional arbitrage from the cost of required targets
and compare equivalent feasible policies, rather than discard user intent.

## Priority and control boundary

Deterministic current order:

1. Hardware/BMS protections and device safety limits remain authoritative.
2. EV Protection and current control constraints override discharge requests,
   including explicit manual discharge in Active when the constraint is actionable.
3. Authorized manual semantic commands use the existing readiness, permission,
   generation, mailbox/worker and executor path.
4. Economic policies constrain the read-only optimizer. Opt-in economic dispatch
   remains below manual control and EV Protection, with live per-write checks.

The optimizer consumes both proposed EV Protection and effective current
constraints. As before, these restrict the first/current slot; future EV charging
is unknown. A later plan interval is never permission to override protection
when that interval arrives. Active EV Protection may supersede manual operation
under its existing documented conditions; this milestone does not change that.

The policy model remains read-only. The separate runtime implements explicit
manual pause/resume, plan revision checks, bounded replanning and retries through
the existing semantic actuator; see the [dispatch contract](HEMS_Economic_Dispatch.md).

## Persistence and API

Existing authenticated `/api/hems/optimizer/config` and GET optimizer routes are
retained. `/hems-optimizer.json` keeps version 1 and its atomic temp/verify/rename
contract and 2048-byte bound. Existing optimizer fields keep their meanings.
The additive config object is:

```json
"policies": {
  "grid_charging_allowed": true,
  "discharge_allowed": true,
  "target_enabled": false,
  "target_soc_pct": 70,
  "target_deadline": 0
}
```

An absent block loads the legacy behavior (both economic permissions allowed,
target disabled); Shadow itself still defaults disabled. A present block must
be complete and correctly typed, with bounded percentages/UTC and a target inside
the existing bounds when enabled. Null/partial/malformed policy objects reject
the configuration. Legacy clients sending a complete old-style config without
the block select the documented legacy defaults; new clients should round-trip
the complete config including policies. Device write permissions are unaffected.

GET adds `target_state`, `target_deadline`, `target_soc_pct`,
`policy_restrictions`, `current_charge_allowed` and optional current power limits
alongside the existing discharge flag. No monetary field, price unit, provider
schema or command API changes. `target_state` is a modeled status, never device
telemetry. Uncalculated/stale plans retain the existing blockers/suppression.

## Validation and remaining gates

Deterministic tests exercise permissions, reserve/ceiling, exact deadline and
target state, infeasibility, short/gapped horizons, PV charge substitution,
efficiency economics, normal-price arbitrage, decorative-category invariance,
current protection/power constraints, stale/unusable inputs, terminal reserve,
JSON legacy compatibility and production atomic persistence/reboot seams.
Frontend tests cover local/UTC/DST conversion and localized message parity;
rendered fixtures cover policy settings/save/reload and mobile layouts with zero
device commands. Existing semantic/security/no-write tests remain required.

No physical WARP/Marstek or live-provider validation is claimed. Physical command lifecycle/sign/scaling/watchdog/reboot and EV Protection
end-to-end validation, provider/TLS smoke tests and ESP32 runtime/heap/stack
measurements remain. The separate dispatch/manual arbitration software is now
implemented and tested; it does not claim physical validation.

## Measured resource delta

Local production comparison with the merged foundation tree (`3309804f`), same
`generic_esp32s3_usb` environment, toolchain and frontend build:

| Resource | Foundation | Policies | Delta |
| --- | ---: | ---: | ---: |
| Reference sketch | 2,830,757 B | 2,837,249 B | +6,492 B |
| Normal OTA | 2,831,120 B | 2,837,616 B | +6,496 B |
| Static RAM | 116,240 B | 116,256 B | +16 B |
| Served app gzip | 316,851 B | 320,752 B | +3,901 B |
| Native DP workspace | 15,480 B | 15,768 B | +288 B |
| Native Plan | 16,552 B | 16,568 B | +16 B |
| Native Config | 40 B | 52 B | +12 B |

Native sizes use the 64-bit host ABI; firmware/static RAM above uses the actual
ESP32-S3 linker output. Version/branch strings and image alignment can change
absolute firmware sizes by a few bytes. No new worker, dependency, scripting,
partition or PSRAM requirement. Existing heap allocation/lifetime remains bounded.

A 48-hour, 2%-grid fixture (192 slots, 52 states) compiled with g++ `-O2`, 31
iterations, measured median **process CPU** time 1,807 → 2,037 µs (+230 µs).
With a 73% target at a non-quarter-hour deadline: 193 slots, 53 states, 2,174 µs.
These are hardware-free host comparisons, not ESP32 timing or headroom claims.
DP complexity remains bounded time × energy-states²; one extra target state and
one possible deadline boundary are the only size increases.

The same compact persisted fixture grows 357 → 487 B (+130 B) with default policy
fields, or 495 B with an enabled dated target. The existing 2048-byte file/request
limit remains. These byte counts describe that fixture, not all configurations.
