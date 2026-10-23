# PR #34 consistency review

Reviewed against integration base `hems-energy-management` at
`693eafe60fb2538ec11019dc0875737eb7e0495c`. The fetched PR head before these
fixes was `bc41747e88cfe3eb0d239e0097a3892b39651ba5`; its two status-document
commits were preserved. PR #34 remains **draft, unmerged and physically
unvalidated**. These are source and deterministic software findings.

1. **Night discharge: confirmed, with a narrower remedy than declaring DPL
   available.** `PowerLimiterInverter::getEligibility()` deliberately returns
   `Nighttime` before reachability checks for solar. The previous aggregation
   required every inverter to be eligible, while `Flow::coordinate()` required
   fresh PV too. Both blocked nighttime discharge. DPL now publishes a distinct
   dormant-solar state only for the supported, enabled solar topology with
   command permission and known device bounds. `Controller::flowDecision()`
   expires it after three seconds. Fresh grid and battery AC measurements give
   `grid - storage = household - actual PV`, so a residual-only battery request
   cannot intentionally increase export even with unknown or nonzero PV.
   No zero-PV sample is synthesized, and the load learner still rejects unknown
   PV. Dormancy permits neither battery export nor a claim of working PV
   actuation; joint export enforcement reports unavailable. Daytime failure,
   disabled command sending, unsupported topology and stale DPL/grid/storage
   observations do not get this exception. Forecast PV remains a forecast,
   capped by the same effective physical ceiling; immediate EV limits still
   override discharge.

2. **EV ownership handoff: confirmed.** The old unblock branch sent another
   Protection-origin Stop whenever coordination was enabled. Completion restored
   `_evProtectionOwned`, and both economic authorization and dispatch rejected
   discharge under Protection ownership. After the existing fresh-false debounce,
   the provider now transfers the existing controlled Stop from Protection to
   Flow under its mutex, only when idle, owned and not uncertain. There is no
   electrical Release/acquire interval. A fresh economic command takes ownership
   through the normal worker. Manual supersession and unrelated ownership are
   untouched.

3. **Flow ownership cleanup: confirmed on withdrawal; automatic Release during
   unavailable coordination is rejected as unsafe.** `releaseEconomic()` could
   never clean up a Flow-origin Stop. Source-specific `releaseFlow()` now uses the
   existing retained-endpoint one-shot cleanup when coordination is withdrawn,
   after an idle mailbox, without waiting forever for unavailable telemetry. It cannot release Manual or
   Protection ownership. A failed cleanup is diagnostic and is not retried every
   tick. While coordination remains enabled but a plan/actuator is unavailable,
   an established controlled Stop is an intentional safety hold, reported as
   `holding_flow_protection`; releasing it could restore autonomous export.
   Invalid economic authority now requests no battery power, including when wear
   acknowledgement is withdrawn. Recovery takes over through the existing
   semantic worker without Release; explicit withdrawal drains Flow ownership.
   Mode, endpoint and permission withdrawal retain their existing cleanup paths.
   Flow Stop authorization is rechecked by the worker after queueing; policy
   withdrawal/pause revokes it, and Flow cannot steal Manual or Protection custody.

4. **Global DPL ceiling: confirmed.** The previous coordinated assignment ran
   after `min(calcTargetOutput(), TotalUpperPowerLimit)` and replaced that bounded
   value. In the supported all-solar AC topology, the effective ceiling is the
   smaller of DPL's global upper limit, the sum of each inverter's
   `min(model rating, configured upper limit)`, and the existing 16-bit range.
   Zero is a real ceiling. `effectivePvCeiling()` supplies this bound to both the
   coordinated DPL request and planner observation; a separate known-bound flag
   prevents zero being mistaken for unknown. Device allocation/reduction logic,
   eligibility, ordering, retries and per-inverter limits remain authoritative.
   The ordinary meter-target request is deliberately replaced: retaining it as
   another minimum would curtail PV needed for permitted battery charging.
   Nonzero lower floors, overscaling, non-metered/ungoverned sources, missing
   configured inverters and mixed DC/storage sources remain unsupported rather
   than being falsely promised this simple aggregate ceiling. Passthrough uses
   the same cap; no second writer or network call was introduced.

5. **Legacy export behavior: confirmed.** Unconditional use of default-false
   `flow.batteryExport` changed plans even with absent/disabled coordination.
   Disabled coordination now retains the old `exportConfigured` permission;
   explicit coordination opt-in selects independent battery-export permission.
   Merely saving the additive disabled block preserves legacy behavior. This
   avoids silently changing existing active dispatch or silently enabling the
   new controller. The UI and migration contract explicitly describe this
   boundary. Legacy compensation-based permission is not represented as joint
   export protection; users wanting the new restriction must explicitly enable
   coordination and review its permissions. The EV opt-out likewise applies
   only when coordination is enabled.

6. **LocalFirst ordering: confirmed.** Curtailment previously erased the surplus
   before the charge-priority test, especially at zero export. The transition now
   allocates physically capped available PV to eligible charging before export
   and curtailment. It checks the actual next representable energy state,
   including exact initial/target insertions, rather than assuming every gap is
   the nominal SoC step. Discharge cannot displace eligible local-first charging.
   Full/charge-restricted batteries can hold. Runtime still uses measured power;
   the remaining difference is at most the representability of the DP's energy
   lattice, not a different priority policy.

7. **Baseline validity: confirmed.** Ignoring a failed hold transition could
   report partial/zero baseline costs and misleading savings. The declared
   reference remains a held battery under the same physical policy and starting
   stock. If any hold transition or covered target is infeasible, `baseline_valid`
   is false and baseline cost, modeled savings and grid-bill comparison are null,
   with `hold_baseline_infeasible`. The optimized cost breakdown remains visible.
   UI money formatters already render null as unavailable. There is no invented
   zero-cost fallback or extra comparison DP allocation. Deterministic validation
   against the documented WARP schedule remains a separate like-state benchmark.

8. **PV-only bootstrap: confirmed limitation; an unpermitted probe is rejected.**
   Measured surplus can disappear after PV is curtailed to load. Raising PV ahead
   of confirmed charging risks export; charging first risks import; neither
   actuator command guarantees a simultaneous physical response. For a policy
   forbidding grid charging and allowing no PV export, no automatic probe is
   authorized. Live coordination reports `pv_charge_waiting_surplus`. The
   planner conservatively excludes charging throughout that horizon while no
   fresh surplus is observed under this policy; it does not count inaccessible
   forecast PV as charge energy or export revenue. Fresh surplus re-enables
   planning; explicit grid-charging permission or a positive permitted PV export
   allowance also makes bootstrap feasible. A separate DPL recovery defect was
   found: acknowledged 0 W was confused with an unknown limit, and a non-producing
   solar source at a zero lower floor offered no increase. DPL now recognizes
   successful timestamped read/set acknowledgements (not the parser's startup
   default OK), and offers known-zero recovery capacity to its existing bounded
   allocator. This may restore PV to permitted loads/export/confirmed charging,
   never beyond the envelope. It does not solve battery startup without an
   authorized sink. Forecasts do not prove available
   surplus. This can forgo useful PV-only charging in strict zero-export setups;
   a future opt-in import/export probe would need an explicit budget and device
   response evidence. The reference preset already permits grid charging.

9. **Unknown wear: confirmed model omission; new active gate is a product
   decision.** Unknown was displayed honestly but economically omitted, so it
   behaved like zero in the objective. Shadow planning remains available with
   that warning. Active *coordinated* economic dispatch and its worker
   authorization now require estimated or explicitly configured wear; explicit
   zero is an acknowledgement, not a claimed measurement. Hard Stop/protection
   remains available. No universal coefficient or extra profit threshold is
   introduced. Legacy dispatch remains compatible until coordination opt-in.
   English/German/French settings explain the gate, and dispatch reports
   `waiting_wear_cost`.

10. **Negative terminal prices: confirmed deliberate model limitation, not
    corrected by blindly removing the clamp.** The table values avoided-import
    inventory and caps it by continuation load; it does not model tail charging
    opportunity, chronology or negative-price headroom. Applying a signed price
    to that discharge-based table would price the wrong energy/efficiency and
    could encourage unjustified emptying. Keep its bounded nonnegative value,
    disclose `negative_terminal_headroom_unmodeled` when the automatic selected
    price is negative, and explain it in all supported translations. Covered
    in-horizon negative prices remain numeric DP inputs. A future headroom table
    needs a credible tail charging model and repeated-replanning evidence; no
    perfect future data is assumed here.

## Regression and validation scope

`test_energy_management.cpp` exercises residual-only night operation with stale
and nonzero actual PV, stale meters, EV restrictions, changing/zero global bounds,
LocalFirst before curtailment and irregular lattice gaps, invalid baseline nulls,
PV-only startup feasibility/recovery and the negative-terminal warning. Existing
closed-cycle/degradation, tariff, forecast, hysteresis, repeated-replanning and
history/DST fixtures remain in place.

`test_economic_integration.cpp` runs the production controller, optimizer,
providers, mailbox and executor with deterministic wire/task seams. It covers
actual nighttime semantic discharge; Protection -> Flow -> Economic handoff after
debounce without Release; safety hold across invalid/unavailable authority;
recovery; one-shot failed Flow cleanup; and Manual/Protection isolation.
`test_shadow_optimizer.cpp` checks absent and disabled migration plus explicit
permission opt-in. `test_economic_dispatch.cpp` checks unknown/estimated/explicit
zero wear. Browser tests save explicit zero acknowledgement and exercise the
existing Monitor/manual-pause/resume flows at four widths. DPL's pure bound and
mailbox cap are exercised directly; radio/physical allocation remains device
validation, not simulated hardware success.

Final commands/results and ESP32 build sizes are recorded in
[the energy-management contract](HEMS_Energy_Management.md#validation-and-device-limits).
No GitHub Actions were polled, and no firmware was flashed or deployed. Runtime
heap, largest contiguous block, stack, latency, DPL responsiveness, meter timestamp
alignment, dawn/dusk behavior, export transients and actual Marstek V148 command
persistence/watchdog/physical response remain unvalidated on this integration.
