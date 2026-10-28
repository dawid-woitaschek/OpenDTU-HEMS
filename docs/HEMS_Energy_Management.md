# Coordinated energy management

This extends the bounded time-slot / stored-energy DP; it does not replace numeric
prices with price categories or minimum-profit gates. Original implementation baseline fetched from
`hems`: **693eafe60fb2538ec11019dc0875737eb7e0495c**. At that source the load was
constant, throughput only broke ties, terminal energy had to remain at least the
planning start energy, export compensation also permitted battery export, and the
storage dispatcher did not coordinate with DPL. These findings were verified in
`Planner.cpp`, `Service.cpp`, `Dispatch.cpp`, `Controller.cpp` and `PowerLimiter.cpp`.

## Defaults, migration and operating goals

The additive `energy_management` object lives in the existing version-1
`/hems-optimizer.json`, with the existing authenticated API, 2048-byte request/file
bound, verified temporary write and atomic rename. Old files remain readable.
Absent new fields default to coordination **off**, automatic learning **off**,
battery export **forbidden for opted-in coordination**, EV protection **on**, zero permitted net export,
unknown/unmodeled wear and zero optional margin/hysteresis. Existing explicit
storage/EVSE permissions, global mode, economic dispatch opt-in and manual pause
remain separate gates. No migration grants a permission or activates new control.
Absent or disabled coordination preserves the legacy `exportConfigured`-based
battery-export permission, including after saving the additive disabled object.
Explicit coordination opt-in switches to independent export permissions; only
then can compensation never grant battery-export permission.
Old firmware ignores the additive object: disable control before downgrading,
because old firmware cannot enforce the new permissions or export coordination.

The household reference button selects PV first, surplus PV export, no battery
export, EV discharge protection, automatic household learning, grid charging and
economic discharge. It clears a dated target and selects the finer existing 2%
energy lattice. It **does not** invent a battery capacity, efficiency, wear rate,
inverter rating, grid connection limit, SoC window or device permission. It does
not enable dispatch or coordinated control. Configure the actual ratings and
permissions, then explicitly opt in. No 01:15–03:15 window or 60% target is a
production rule; those appear only in a deterministic comparison fixture.

Disable old **WARP battery-writing rules** before granting HEMS storage control.
WARP can retain EV control. HEMS adds no automatic EVSE scheduler. The alternative
settings are economic operation, local PV first, or surplus export before PV
charging. The independent grid-charging, discharge, PV-export, battery-export and
EV-protection permissions remain authoritative in coordinated mode. Unknown wear
is not a verified zero; shadow plans omit it with a warning. Active coordinated
economic dispatch requires an estimate or explicitly configured coefficient,
including explicit zero acknowledgement. Legacy dispatch retains its old gates.

The planner caps battery power by imported profile operation maxima and caps PV
by the observed effective DPL ceiling as well as user limits. Physical battery
charge/discharge limits and the total PV inverter AC rating are
independent of the maximum **net export at the grid connection**. The latter can
be zero or another user value; there is no hard-coded 800 W rule. The DPL meter
regulation target remains a separate DPL setting, not an export permission or a
joint export guarantee. In coordinated mode the solar request follows the flow
envelope for permitted local use/export instead of DPL's ordinary meter-target
request; acknowledged zero limits remain known and DPL can recover a
non-producing solar source from zero through its bounded allocator, without an
unbounded wake/probe. Parser default OK without a timestamp is still unknown.
DPL retains its global `TotalUpperPowerLimit` (including zero), each
inverter's configured/model maximum, communications, ordering and retries. The
same effective ceiling bounds DP PV; zero is distinguished from an unknown bound.

## Measurement and execution contract

At the grid connection: grid power is positive for import; solar AC power is
positive generation; storage AC power is positive charging and negative
discharging. Thus `household = grid + PV - storage`. DC storage/charger signals
are never added to that AC balance. Only classified AC solar Hoymiles sources
participate in this coordinator. Mixed DC/battery inverter topologies are not
silently treated as AC PV.

Every second, the controller computes a bounded envelope from fresh grid, solar
and storage telemetry, current SoC, permissions and protection. Grid freshness
is at most 5 s, AC PV at most 10 s, and coordination rejects storage older than
10 s. The existing provider freshness/fault/authorization gates also apply.
Without battery-export permission, discharge cannot exceed the measured residual
consumption after PV. If DPL indicates possible usable curtailed PV, discharge
is held rather than substituting battery energy for that PV. EV protection
independently forbids discharge immediately and retains the existing two-minute
fresh-false stop debounce; the setting can explicitly disable that policy.

Local-first operation serves loads, then eligible charging, then permitted export
up to the net limit. Charging eligibility includes the current SoC ceiling,
charge permission and device/configuration limits. Economic mode can choose
export/curtailment instead of a costly charge. Export-first mode reserves the
permitted surplus for export before PV-only charging. A PV-only charge request
is bounded by **measured** surplus, not forecast power or guessed inverter
headroom. After curtailment it may have to wait for confirmed surplus; it does
not intentionally draw grid power to discover unmeasured PV availability. With no
grid charging and no permitted PV export, a fully curtailed system cannot safely
bootstrap charging. Planning excludes such charging throughout the horizon until
fresh surplus is observed, and runtime reports `pv_charge_waiting_surplus`.
Grid charging or a positive permitted PV export allowance removes this startup
restriction. No unconfirmed command is counted as an electrical load.

DPL consumes a small mutex-protected envelope with a three-second expiry. No
network operation, planner or second Hoymiles writer is added to its loop. The
common solar limiting path clamps requests, bypasses ordinary regulation
hysteresis for the envelope, and applies it to MQTT passthrough too. Direct
inverter limit/power/restart requests through HTTP or MQTT are rejected while
coordination owns the output path. The normal DPL device upper/lower limits
remain in force. Supported coordinated topology requires governed solar sources
behind the grid meter, zero lower output floor, no overscaling and aggregate AC
power fitting the existing 16-bit DPL range. Unsupported topology is reported,
not presented as a working export limiter.

The PV cap uses **observed** battery charging, with the more restrictive pending
battery action. It cannot assume a requested charge has already started. DPL can
curtail PV but cannot undo battery export after solar output reaches zero. The
storage request therefore also decreases/stops. Worker authorization recomputes
live constraints immediately before each bounded register write, including manual
power requests. Release and target-SoC commands that would hand regulation back
to autonomous battery control are rejected while coordinated control is enabled.
Economic invalidation does not release an owned battery into autonomous export;
the guarded Stop path can act on excessive discharge or unsafe retained charging.
Its retries require a newer successful poll, a 30 s interval after the first
attempt, and at most three attempts per UTC quarter-hour while the violation
persists. Actual safe telemetry resets that budget.

Missing/stale/inconsistent balance data yields a zero PV envelope and no
discharge. With fresh grid and storage data, permitted grid charging can continue
as a consumption-only action while PV measurement/control is unavailable; **joint
export enforcement remains unavailable**. A missing meter does not authorize
that fallback. At night DPL can publish a distinct supported dormant-solar state.
Fresh grid and battery measurements then bound discharge by `grid - storage`,
the actual residual consumption even with unknown/nonzero PV. No zero-PV sample
is fabricated, battery export is prohibited in this fallback, and joint PV export
enforcement still reports unavailable. Stale meters/storage, disabled commands,
unsupported topology or daytime actuator failure do not get this exception.
Dawn/dusk transitions and battery autonomy/watchdog behavior require validation.

After EV protection's debounce, an idle, certain Protection-owned Stop transfers
to Flow without a physical Release; economics can then resume through the normal
worker. Missing authority retains a reported controlled safety hold rather than
restoring autonomous export. Explicit coordination withdrawal uses source-specific
one-shot Flow cleanup, preserving Manual and unrelated Protection ownership.

Manual pause and manual ownership prevent automatic storage takeover. Global
mode/permission revocation preserves the existing bounded courtesy-cleanup
lifecycle, after which enforcement is explicitly unavailable. Failures, missing
actuators, uncertain ownership, manual pause, unready topology and stale data are
reported in `export_enforcement_available` and `coordination_state`; networked
control does **not** promise instantaneous, transient-free zero export. A
battery's own export setting is never used as proof of the joint grid limit.

## Local household profile

The optional learner integrates trapezoidal AC power samples into UTC 900-second
energy intervals. Gaps over 30 s, invalid flow balances, clock jumps or missing
measurements break integration. A bin needs at least 80% valid coverage. Missing
consumption stays unknown; it is not stored as zero. There are seven fixed daily
records of 96 local time-of-day bins, with coverage and last-update metadata.
Forecasts require three valid matching observations within seven days. Weights
favor recent and comparable days; weekday/weekend observations are separated
when three matching day-type observations exist. Otherwise comparable days get
extra weight without pretending that the available weekend coverage is complete.

Confirmed EV charging without reliable measured EV power is excluded. WARP3
Smart allowed current is **never** converted to measured charging power. A
separate reliable power signal can be subtracted; unknown charging status also
excludes a configured EVSE's intervals. Gaps use valid comparable history or the
explicit constant fallback. Reliable future EV restrictions can enter the DP as
intervals; no such schedule is invented from allowed current or vehicle presence.

UTC integration prevents DST folds/gaps from manufacturing elapsed energy. Civil
bins in a fall-back fold are coverage-weighted; spring gaps retain unknown bins.
A timezone-rule change invalidates the civil history. Reboots lose only the
unfinished bin and observations since the last checkpoint. History uses a
versioned, checksummed fixed-size binary checkpoint, streaming verification and
atomic rename, at most every six hours on the existing energy worker. Failed
checkpoints retain the previous file; corrupt/wrong-size/version files fall back.
There is no raw high-frequency history, per-sample flash write, database, cloud
learning or additional task/computer.

Each DP slot gets its own load estimate, source and coverage. No load dimension
is added to the energy-state lattice. Forecasting is an estimate: three recent
days can be better or worse than a fixed baseline.

## Costs, terminal inventory and hysteresis

For internal stored-energy changes `deltaWh`, reference throughput is
`sum(abs(deltaWh)) / 2000` kWh. Charging internally by Q kWh and later discharging
by Q kWh incurs Q reference kWh. Dimensionless EFC divides reference kWh by a
fixed reference capacity: `efc_capacity_wh`, or the configured nominal battery
capacity when that field is zero. It never divides by the selected SoC window.

`degradation_micros` is currency micros per reference kWh; the independent
`buffer_micros` is an optional conservative throughput margin in the same units.
There is no universal default coefficient or installed-system-price estimator.
The DP includes these costs in its primary objective and retains absolute
throughput only as a tie breaker. It keeps half-nano currency precision for the
half-throughput calculation and rounds aggregate reports after summing throughput.
AC import/export costs and charge/discharge efficiencies remain separate. PV
opportunity cost is its foregone export revenue, not a second wear/loss charge.
No calendar/rainflow/temperature model, arbitrary switching penalty or unsupported
auxiliary-consumption estimate is added.

The terminal start-energy floor is removed. A bounded value table over the same
energy states values deliverable AC inventory above the user reserve, capped by
an explicit 1–48 h expected continuation-load window (default 12 h). Expected
load uses the current horizon's load profile average. An explicit continuation
price can be supplied. Otherwise fully covered future tariff intervals are used:
the lowest future import price caps refill value when grid charging is permitted,
or a median future price is used without refill permission. If that tail is
missing, a nonnegative covered-horizon median is an **assumption**, not fabricated
future data. The selected price, demand bound and source are reported. Known
zero/negative refill prices can legitimately imply zero marginal inventory value;
users should review the fallback assumption for their installation. Negative
selected automatic continuation prices emit `negative_terminal_headroom_unmodeled`:
the table deliberately does not value empty headroom for future paid charging.
Simply allowing a negative avoided-import salvage price would model the wrong
charging/efficiency opportunity.

All objective comparisons include the difference between final and starting
inventory values, from the same table. No end floor follows a rising start SoC.
Minimum SoC and covered target/deadline constraints remain binding. The reserve,
finite continuation demand and conversion/wear costs bound fill incentives; flat
prices do not produce free terminal fill. Terminal inventory estimates are
separate from the grid bill and are never labeled realized cash savings. The
hold baseline has the same starting stock; differing final stocks are reconciled
through the common inventory valuation. The reported grid-bill difference alone
is not an equivalent-inventory economic improvement. If holding is infeasible
under the selected LocalFirst policy or a covered target, baseline cost and both
savings comparisons are null with `hold_baseline_infeasible`, rather than silently
using a zero-cost or partial baseline.

Plan-change hysteresis is optional, zero by default and outside the energy-cost
model. A retained schedule is re-evaluated from the **current** battery state on
the candidate's fresh tariff, PV, load, permissions, limits, deadlines and terminal
table. A newly exposed horizon tail uses fresh candidate actions. Infeasible or
uncovered current actions cannot be retained. Runtime protection and limits do
not wait for monetary hysteresis. There is no minimum spread or whole-plan-profit
activation gate that discards useful multi-slot strategies.

## Validation and device limits

`make -C test test` includes the production planner/coordinator/profile scenarios,
existing controller/provider/worker/executor tests and new real persistence and
worker-revocation cases. The fixtures cover required charge quantity, expected PV,
zero/800 W export, full battery curtailment, battery-induced export, positive PV
compensation without battery permission, alternatives, load drops, stale data,
actuator failure, EV gaps, known future EV restrictions, negative prices,
opportunity cost, precision, repeated rising-SoC replanning, terminal fallbacks,
hysteresis feasibility, opt-in, legacy JSON, midnight/DST and bounded persistence.
The 95%/95%, 3 ct/reference-kWh closed-cycle fixture rejects 15→18 ct/kWh and
accepts 15→35 ct/kWh.

The repeatable benchmark uses the same initial stock, realized consumption,
prices, efficiencies, wear and inventory valuation for all policies. The example
WARP policy charges near 60% at 01:15–03:15 and serves the expensive morning.
This fixture is deliberately deterministic, not evidence of guaranteed household
savings:

| Repeating-day policy | Import bill EUR | Wear EUR | Inventory adjustment EUR | Comparable objective EUR |
| --- | ---: | ---: | ---: | ---: |
| Learned profile | 0.675874 | 0.036000 | 0 | 0.711874 |
| Fixed load | 0.942400 | 0 | 0 | 0.942400 |
| Example WARP schedule | 0.771789 | 0.049200 | -0.102600 | 0.718389 |

Profile load MAE is 0.0104 W versus 57.3333 W for the fixed fallback in that
repeating-day fixture. When the actual day changes to a flat load, profile MAE is
57.3229 W and the fixed fallback's is zero. The forecast is not universally
superior. Separate tests demonstrate different charge quantities for different
required morning loads and less night stock with expected midday PV.

English/German/French settings and summaries distinguish operating goals,
forecast/fallback coverage, current action/reason, restrictions, the horizon charge
target, projected imports/exports, estimated wear, optional margin, inventory,
curtailment and planned versus commanded operation. Advanced JSON includes detailed
assumptions, topology state and history checkpoint diagnostics.

See the resource measurements below. Host fixtures and compiler layouts do not
measure live ESP32 heap, stack high-water marks, planning latency, network jitter,
export transients, TLS headroom or physical Marstek behavior. These still require
hardware validation; no device was flashed, deployed or operated by this change.

## Historical ESP32-S3 resource budget (before PR #35 integration)

Representative local `generic_esp32s3_usb`, 8 MB flash, **no PSRAM** builds use the
same installed toolchain/dependencies. The base is an archived checkout of the
recorded SHA with its own original webapp build. Firmware sizes also include
version strings and linker alignment; these are compile/link measurements.

| Resource | Base | Coordinated | Change |
| --- | ---: | ---: | ---: |
| Static RAM | 117,472 B | 119,760 B | +2,288 B |
| Firmware flash | 2,868,625 B | 2,901,433 B | +32,808 B |
| Served app gzip | 331,311 B | 339,320 B | +8,009 B |
| DP workspace | 15,768 B | 16,200 B | +432 B |
| Plan | 16,552 B | 18,672 B | +2,120 B |
| Result object | 16,896 B | 19,104 B | +2,208 B |
| Optimizer service object | 92 B | 2,320 B | +2,228 B |

The fixed history is **2,108 B** (checkpoint **2,112 B**, including checksum).
The learner with its integration scratch is **2,160 B** inside the service.
Forecast load/source/coverage adds **8 B per slot**, at most **2,048 B per Plan**.
There is no extra forecast array. The coordination mailbox is **32 B** on this
ESP32 ABI. These numbers come from ESP32 compiler object layouts, not host
`sizeof` or live device performance.

The largest individual optimizer object allocation is **19,104 B**. At the
learning-enabled publication peak the bounded object allocations are two Results,
two existing 5,184 B Series, one 16,200 B DP workspace and one 2,160 B learner
snapshot: **66,936 B**, versus **59,928 B** in the base (**+7,008 B**). Constant
load mode does not allocate the learner snapshot, and idle worker notifications
allocate no planning scratch. Monetary hysteresis reuses the candidate Plan;
terminal price sorting reuses parent scratch before parent generation. No third
Plan or new worker is added. Task stack reservations, flash partition and
dependencies are unchanged; bounded local snapshots/authorization checks still
require device stack-watermark validation.

Allocator headers, SDK mutex backing, transient configuration/response JSON and
concurrent network/WebAPI/TLS pools are additional; these object sums are not a
claim of total live heap headroom. The existing energy-worker sequencing releases
planning scratch before provider HTTP/TLS work. Existing provider payload/JSON
bounds remain unchanged. Device measurements must still verify minimum free heap,
largest free block (including the larger Result), worker/scheduler stack watermarks,
DPL responsiveness, planning latency, communication recovery and actual export
transients with the physical Marstek and Hoymiles.

Planning complexity remains `O(T × S²)`, with `T ≤ 256`, `S ≤ 54` (746,496
candidate transitions at the maximum). Learning/forecast lookup is at most seven
small daily records per slot; terminal valuation adds a bounded tariff scan/sort
and one value per energy state. Extra monetary/flow arithmetic increases the
constant factor; no ESP32 latency or battery/network response timing is inferred
from host execution time.

The original separate integration base was configured for normal build/test jobs
without channel publication. PR #34 now targets `hems`, using its existing build/channel workflow.
No GitHub Actions were polled or awaited during integration. No device was flashed
or operated.

The [PR #34 consistency review](HEMS_PR34_Review.md) records the ten investigated
concerns, remedies, rejected unsafe alternatives and regression invariants.

Review follow-up validation on 2026-10-23: all 35 native executables passed
(`make -C test -B -j4 test`, followed by a complete run after the final ownership
checks); production configuration and semantic/API/TLS surface checks, C++ lint,
frontend tests/lint/format/type/build and intercepted browser checks at
320/360/768/1440 px passed. The `generic_esp32s3_usb` no-PSRAM firmware build
passed after the final source changes. `GITHUB_ACTIONS=true` skipped only the
redundant automatic web build; the actual pinned frontend build was run locally
first. The sizes above describe this reviewed source, with the original base
comparison unchanged. No remote Actions monitoring or physical validation occurred.


## Hardened hems integration (2026-10-28)

PR #34 is merged into `hems` after rebasing its existing branch onto `49a2526b`,
which contains PR #35 runtime/PSRAM hardening, refreshed PR #31 CMT and PR #36
cleanup. Documentation conflicts retained the newer runtime, radio, cleanup and
hardware evidence; obsolete separate-branch status was replaced. No PR #33 work
was replayed. The numeric global DP and all economics/flow behavior above remain.

Expanded candidate/retained Results (including Plans), DP Workspace and Series
already inherit `hems/Memory.h`; the optional 2,160-byte learning/checkpoint
snapshot now uses it too. With PSRAM present, these allocations request
`SPIRAM|8BIT` and fail without INTERNAL fallback; no-PSRAM targets use the normal
8-bit heap. The fixed retained learner remains in static service storage, like
retained provider Series. Constant-load mode avoids snapshot allocation. No third
Plan, new task, stack reservation, allocator-policy tuning or DP redesign is added.
Stable Result publication and scratch release before provider TLS remain intact.

Physical validation of the combined build remains pending: Marstek V148 sign/
scale, acquire/Stop/Release and watchdog/persistence, WARP control/EV protection,
DPL/grid-export transients and timestamp alignment, load learning and optimizer/
dispatch decisions, planning latency and runtime INTERNAL/PSRAM/stack/TLS headroom.
Use Device A with 2 MB quad PSRAM (`generic_esp32s3_usb_quad_psram`) and the
[hardware validation runbook](HEMS_Hardware_Validation.md). No-PSRAM USB is a
compatibility build. The accepted PR #35 run does not validate these later changes.

Local integration checks on 2026-10-28 passed all 37 native executables and the
control-surface guard, plus focused memory/optimizer reruns after the new failure
regressions. Coverage includes expanded Plan/Profile capability placement, no
INTERNAL spill and retry after learning-snapshot exhaustion, alongside existing
CMT, provider, controller, mailbox, executor, DP, economics and flow cases.
Production configuration, firmware channel/provenance and catalog-generation
checks passed. Node 26 / pinned Yarn 4.18.1 frontend tests, type check, production
build, oxlint, focused ESLint/Prettier and repository/focused C++ lint passed.
All three intercepted-API browser suites passed: HEMS/presentation and economic
flows at 320/360/768/1440 px, plus firmware/catalog regressions at 360/1440 px.
These checks send no commands to physical devices.

Xtensa GCC 8.4.0 layout checks confirm Series 5,184 B, Workspace 16,200 B, Plan
18,672 B, Result 19,104 B, learner 2,160 B and service 2,320 B. Two Results, two
Series, Workspace and learner snapshot can overlap at **66,936 B**, excluding
allocator/SDK/JSON/network overhead. These dynamic bounded objects all use the
explicit memory model. Selected optimized USB compiler frames are `calculate`
368 B, `evaluateRetained` 208 B, `Service::due` 560 B and `Service::run` 832 B;
these are individual compiler frames, not full call chains or measured watermarks.
No large Plan/Profile temporary appeared on these production stack frames.

Both local PlatformIO 6.2.0 builds passed with espressif32 6.13.0 / Arduino-ESP32
2.0.17 and the newly built full webapp assets (`app.js.gz` 339,332 B). The local
`GITHUB_ACTIONS=true` setting skipped only the frontend freshness hook; assets were
built/type-checked separately before firmware. The compiled application source
matches code commit `a52504cf`; subsequent edits are documentation only. These
are compile/link results, not release/OTA identity or hardware acceptance:

| Environment | Reported static RAM | Reported firmware flash |
| --- | ---: | ---: |
| `generic_esp32s3_usb_quad_psram` | 120,852 B | 2,912,941 B |
| `generic_esp32s3_usb` | 120,592 B | 2,910,505 B |

Radio, provider/runtime hardening and cleanup support-snapshot paths match the
`49a2526b` base exactly. Comparing PR #34's production/UI files with its original
head finds only the three optimizer allocation paths changed by inheritance of
PR #35 and learning-snapshot placement. Obsolete separate-base CI scaffolding
was removed; the existing `hems` workflow is unchanged. `git diff --check` passed.
GitHub Actions were not queried or awaited. No firmware was flashed or hardware
operated, and no PR #33 branch or implementation was changed.
