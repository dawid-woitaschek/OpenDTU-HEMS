# KD01 exactness review of PR #40

## Decision

The interpolation in `633b9646` is **demonstrably economically wrong**, not an
exact algorithm or a certified approximation with a product error budget. KD01's
25 Wh examples alone did not establish optimality. Replace it with exact global
whole-Wh DP. A ready result now minimizes the existing modeled half-nano objective
and then absolute internal throughput over all feasible whole-Wh trajectories.
This is a software/model guarantee, not validation of physical battery behavior.
The separate KD02–KD04 / KD06–KD08 policy defects remain; broader KD05 remains open.

## Independent evidence

`test/optimizer_reference.h` exhaustively enumerates inventories and actions and
calculates AC energy, import/export, curtailment, efficiencies, wear and terminal
cost independently. It calls no production transition, retained evaluator,
convolution, interpolation or reconstruction helper. Prices/energy in the test
bounds allow direct integer products, unlike production's split money arithmetic.
The oracle compares the primary objective and secondary throughput; a different
first action at an equal objective is not itself a failure.

Against the old `633b9646` implementation:

- 24 seeded eight-slot low-stock cases at 5/10/15/20 kWh: **6 unequal objectives**,
  worst observed loss **EUR 0.001726583**, mean EUR 0.000139358. Cases included sharp
  positive/negative prices, PV, 95%/100% efficiencies, wear and export limits.
- 96 eight-slot cases with a wider initial SoC distribution: **2 unequal
  objectives**, worst observed loss EUR 0.000663158, mean EUR 0.000008783.
- A 20 kWh, 10–90% battery starting at reserve, 2.5 kW charge/discharge,
  95%/95% efficiency, 300 W load, EUR 0.03/reference-kWh wear, no export or PV,
  zero terminal price, and alternating EUR 0.40/0.60 per kWh:

| Slots | Exact objective EUR | Interpolated objective EUR | Economic loss EUR |
|---:|---:|---:|---:|
| 8 | 0.262888444 | 0.262888444 | 0 |
| 24 | 0.788665332 | 0.793304332 | 0.004639000 |
| 96 | 3.154661110 | 3.167775996 | 0.013114886 |

The 96-slot error loses approximately **2.95% of the available modeled savings**
against the EUR 3.60 hold baseline. This is an accumulation counterexample, not
just a different equally good action. These are observed maxima in the stated
fixtures, **not a global worst-case bound**. No useful policy regret bound was
established for the interpolation, so small average losses do not justify it.

### Why interpolation is not exact

Even at unity efficiency, no wear, zero terminal value and no export, the final
slot's continuation cost for a 300 W / 15-minute load at EUR 0.60/kWh is
`600000 * max(75 - (energyWh - reserveWh), 0)` nano currency. It has a kink at
`reserveWh + 75`, which need not be a chosen sample. If neighboring samples are
reserve and reserve+132 Wh, the interpolant at reserve+75 Wh is approximately
EUR 0.019432, while the true value is zero. No linearity theorem applies between
those samples. Efficiencies, rounding, targets and competing price periods add
breakpoints; affine slot costs do not make the Bellman minimum affine there.

A symbolic piecewise-linear representation would need propagated breakpoints,
competing-envelope intersections and the existing integer-rounding residues.
A fixed coarse breakpoint budget cannot silently drop them and retain exactness.
The replacement represents **every whole Wh** instead; larger problems are
explicitly refused rather than approximated.

## Exact bounded replacement

1. Use every integer inventory in the power/policy-reachable horizon range, up to
   32,769 inventories (32,768 Wh span). Preserve all current physical transition
   checks, exact in-horizon deadlines and existing Ws/money rounding. The legacy
   `soc_step_pct` field round-trips but no longer selects state/action resolution.
2. Compute exact min-plus Bellman updates. Within one import/export/curtailment
   regime, charge costs repeat affinely after `eta_c/gcd(eta_c,1000)` Wh and
   discharge costs after `1000/gcd(eta_d,1000)` Wh. For example, 95% efficiencies
   give periods 19 and 20. This follows because the AC energy shift is an integer
   number of Wh, so the integer money shift is exact even for negative prices.
3. Evaluate **every admissible integer action** when forming the runs; merge only
   successive actions in the same residue with the same observed cost difference.
   Thus even the run construction does not extrapolate an unchecked action cost.
   Each verified run uses an exact monotonic-deque sliding minimum. No convexity
   of tariffs or future values is assumed. Non-translation-invariant Local First
   transitions entering/leaving the SoC ceiling are evaluated separately.
4. Apply only a proved inventory dominance rule: under nonnegative import/export
   prices and optional economic charging, extra inventory beyond all possible
   discharge, valued terminal demand and target cannot lower cost. Removing its
   extra charge cannot increase import/export/wear cost and reduces throughput.
   Keep measured stock. Disable this pruning for negative prices and mandatory
   flow preferences. It changes neither the optimal objective nor its tie-break.
5. Select the exact final optimum and reconstruct using iterative Hirschberg
   divide-and-conquer with three cost/throughput rows and ten bounded PSRAM frames.
   Midpoints minimize exact forward+backward costs. Endpoint reachability prunes
   only impossible states. Recheck actual transitions, total cost and throughput
   against the optimum before publishing. No hold fallback hides a failed solve.

The Bellman recurrence considers the complete feasible graph. Run convolution is
an algebraic reorganization of that minimum, not a cost approximation. Induction
on slots proves each finite row value exact. Hirschberg's additive midpoint
identity preserves the optimum and tie-break. This proves the ready-result
optimality guarantee within the existing whole-Wh model, including integer
rounding. It does not make the separate unresolved policy semantics correct.

## Device A resources and acceptance boundary

Reference: ESP32-S3, 8 MB flash, **2 MB quad PSRAM**,
`generic_esp32s3_usb_quad_psram`. Both metadata and row/deque storage request PSRAM
explicitly, with no INTERNAL retry. No-PSRAM compatibility reports
`optimizer_requires_psram`. No task, stack reservation, TLS policy, provider or
Remote Diagnostics / telemetry implementation changed.

Scratch uses **38 bytes per inventory**: three int64 cost rows, three uint32
throughput rows and a uint16 deque. Native metadata is 5,360 B, compile-capped at
8 KiB. The ten reconstruction frames are in PSRAM; there is no recursive task-stack
chain or T×S parent table. At 10–90% full ranges, native workspaces are:

| Battery | Inventories | Workspace B |
|---:|---:|---:|
| 5 kWh | 4,001 | 157,398 |
| 10 kWh | 8,001 | 309,398 |
| 20 kWh | 16,001 | 613,398 |
| 20 kWh, 0–100% | 20,001 | 765,398 |

The last case needs a contiguous 760,038 B row/deque block. Combining it with
approximately 50,752 B other planner scratch/results and unchanged telemetry
rings/payload/JSON maxima (873,216 B) gives approximately **1.61 MiB** before
allocator overhead and other allocations. This is budget arithmetic, not device
peak/free/largest-block measurement. At the absolute 32,769-state cap, native
workspace is 1,250,582 B; it cannot coexist with every separate telemetry maximum
inside 2 MB. Allocation failure must defer the solve without INTERNAL spill.

Worst-case work is O(T × S × (R_charge + R_discharge) × log T), capped explicitly
at **600,000,000 counted work units**, including reconstruction. Cost-period
constants are at most 1,000. The counter charges row clearing, action evaluations,
window/source visits and midpoint searches; deque removals are amortized bounded
by source insertions. This is an operation bound, not a wall-clock deadline.
`optimizer_state_capacity_exceeded`, `optimizer_work_limit` or
`insufficient_memory` means no ready plan is published. The model is never
silently coarsened. These are real availability limits, including potentially
expensive valid configurations; exactness is not a guarantee that every parsed
configuration fits this device's budget.

Optimized native reference timing for the 20 kWh alternating-price regression was
approximately 28 ms / 6.75 million work units at 24 slots and 464 ms / 111.04 million
units at 96 slots, with 76,534 / 289,942 B workspace after exact pruning. A separate
5/10/20 kWh day/night fixture at 95% efficiencies, 2.5 kW limits, 500 W load,
50% initial SoC, zero terminal price and EUR 0.03 wear produced:

| Battery | 24 h native ms | 48 h native ms | 48 h work units | 48 h workspace B |
|---:|---:|---:|---:|---:|
| 5 kWh | 406 | 1,077 | 249,932,593 | 157,398 |
| 10 kWh | 558 | 1,606 | 379,394,946 | 309,398 |
| 20 kWh | 591 | 2,316 | 535,209,258 | 613,398 |

These fixed fixtures fit the budget; the last uses approximately 89% of it.
More expensive valid cases can still be refused, as the 99.9%-efficiency work-limit
fixture demonstrates. The rejected sampler took approximately 3–14 ms on the
same day/night fixtures. This is substantially slower and must not be presented
as an ESP32 timing estimate. Native compiler frames were
560 B for calculate, 384 B for sweep, 56 B for convolution and 144 B for the
ceiling-edge helper; these are not Xtensa call-chain watermarks.

**Software correctness is established within these explicit limits; Device A
runtime acceptance is not established.** Measure worst-case planning latency,
watchdogs, DPL/shared-worker responsiveness, INTERNAL/TLS headroom, stack watermarks
and PSRAM largest/free blocks under concurrent telemetry before product acceptance.
The draft PR is not physical validation or an on-device performance approval.

## Focused checks

`make -C test optimizer-optimality-test ARDUINOJSON=../work/ArduinoJson/src`
compiles optimized native code and compares realistic ranges against the independent
exhaustive oracle: 48 mixed-price/PV/export fixtures; targets and split deadlines;
85–99.9% efficiencies, wear and negative export prices; Local First saturation;
24/96-slot accumulation regressions; and explicit state/work exhaustion.

KD01's two original witnesses and the original coupled KD05 witness remain normal
PASS assertions. Six other unresolved audit case bodies remain unchanged/XFAIL.
The existing action, shadow, energy-management, memory-failure, service and remote
sender/HEMS checks remain focused regression evidence. The realistic oracle suite
with `--quick` and the action suite also pass ASan/UBSan; leak detection is disabled
because LeakSanitizer cannot run under this workspace's ptrace environment.
No GitHub Actions polling,
firmware build, physical validation or broad audit fixes are included.
