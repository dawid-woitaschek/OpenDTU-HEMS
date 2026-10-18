# HEMS semantic control

OpenDTU-HEMS is supervisory home-energy automation. It is not the primary safety controller of connected hardware; device protections remain authoritative. A failed command or lingering external-control mode is an operational problem. Operators can remove a wallbox from Modbus control or independently disconnect/reset storage.

## Permissions and modes

Storage control and EVSE control independently select **Monitor only** or **HEMS control**. These permissions affect the HEMS logic as well as the provider. Telemetry remains available in either permission mode.

| Storage | EVSE | Permitted control |
|---|---|---|
| Monitor only | Monitor only | Observation and policy proposals |
| HEMS control | Monitor only | Storage commands; EV charging telemetry still protects storage |
| Monitor only | HEMS control | EVSE commands; storage remains observational |
| HEMS control | HEMS control | Both device types |

Global modes:

- **Disabled:** no automatic control; preserves existing disabled semantics by stopping HEMS provider polling. Known ownership gets one best-effort release attempt.
- **Monitor:** telemetry and policy proposals; manual semantic commands for writable targets; no automatic command producer.
- **Active:** also permits automatic policy commands for writable targets. Manual storage commands pause opted-in economics until explicit Resume; EV Protection retains priority.

Mode is persisted in the existing HEMS configuration. Legacy enabled settings become Monitor, legacy disabled settings remain Disabled, and invalid mode values fall back to Monitor. Device write permissions retain their existing separate persistence domains and default to Monitor only.

## Normal readiness

Manual control requires target HEMS-control permission, a configured/enabled provider, a compatible control profile supporting the requested operation, reachable hardware, fresh valid telemetry, and an available worker/mailbox. Automatic commands additionally require Active. Input bounds and the acquire/operation/release plans are validated before queueing and again by the bounded executor.

Identity, replay eligibility, reboot recovery, persisted ownership, operator attestation and temporary sessions are not prerequisites. No normal actuation writes recovery journals or reads device identity.

The authenticated admin endpoint `POST /api/hems/command` accepts only semantic target/action/value requests (form field `data` containing JSON):

```json
{"target":"evse","action":"set_current_a","value":6}
```

Storage actions: `set_charge_power_w`, `set_discharge_power_w`, `set_target_soc_pct`, `stop`, `release`. EVSE actions: `set_current_a`, `stop`, `release`. No raw-register write API exists. Backend metadata supplies supported actions and min/max/step/unit to the UI; the backend planner remains authoritative.

## Worker and failure behavior

Manual and automatic commands share one depth-one mailbox, provider worker, Modbus client and semantic actuator. Pending/running work is never overwritten. Generation checks and origin-aware authorization run immediately before each bounded write. Leaving Active revokes queued/running automatic work; permission/configuration changes revoke stale generations.

Transport errors, Modbus exceptions, planning errors and readback mismatches are recorded separately. A failed command closes the client and invalidates operational telemetry, then schedules a fresh poll. Normal polling continues. Fresh reachable telemetry automatically restores control; there is no acknowledgement or permanent fault latch. An uncertain acquire is retried on a subsequent command.

Release is an operational courtesy. Changing HEMS control to Monitor only or changing endpoint/profile retains the known old endpoint/profile in RAM and attempts its release once on the same worker. Success and failure are logged; both discard the retained context so the new configuration remains usable. A running-command/configuration race retains the actual old endpoint at completion. The courtesy release is the only write allowed after permission withdrawal; subsequent normal writes remain prohibited.

Reboot starts with no reconstructed ownership. Existing `hems-recovery-*` files from #22 are ignored, including corrupt/pending files. They are visible to authenticated File Management and may be downloaded/deleted there. They never block control. No automatic filesystem formatting is added.

## EV protection

Fresh charging=true immediately proposes discharge protection. Fresh charging=false must remain continuously fresh-false for 120 seconds before releasing protection. Charging resumption cancels that debounce. Stale/unknown telemetry never means definitely not charging and cannot clear established protection.

The policy receives target control capability. Only Active plus Storage HEMS control makes its constraint actionable. EVSE write permission has no effect on observing charging. A monitor-only WARP can protect a writable storage device.

The actionable constraint rejects new discharge-power commands while protection applies, without rejecting charge-power commands. The reference storage profile has no dedicated discharge-permission operation, so **semantic stop** is sent only when fresh valid storage AC-power telemetry shows actual discharge (negative normalized power). Charging and idle storage receive no Stop. After the debounce clears, protection attempts **release** only if its Stop actually attempted a device write and the stopped mode has not been superseded by charging or a later manual operating command. Queueing or cancelling a Stop alone creates no release obligation. Status/UI distinguish the proposal, effective discharge constraint and command result. Observed discharge can trigger another bounded Stop attempt at most once per five seconds. No tariff or automatic EVSE policy is installed yet.

## Results and runtime profiles

The manual-control UI reports requested operation/value, command state/source, transport result/Modbus exception, register readback verification, completion uptime/age, and current device telemetry with freshness. It explicitly distinguishes **Modbus write verified** from **physical behavior observed**. Wait for a successful telemetry poll after completion before interpreting physical behavior.

The bound profile diagnostic shows ID, schema, telemetry/control support, concrete unavailability reason, identity/recovery metadata, source revision and a diagnostic content fingerprint. No watchdog lifecycle metadata is currently declared by either reference profile. Schema-v1 profiles remain telemetry only; missing control/release and invalid metadata have specific explanations.

Repository profiles are never installed automatically. Import a downloaded JSON in HEMS → Devices / Profiles. Importing the same ID with replacement enabled updates the runtime copy; the UI previews capability changes, the backend validates compatibility with bound consumers, and providers reload after the store lock is released. Pending work is cancelled, old ownership gets a best-effort release, and a new fresh poll enables new capabilities. No reboot is required.

## Reference profiles and physical validation

WARP3 Smart: `set_current_a(6..32 A)`, stop (`0 mA`), release (`32000 mA`, neutral/default Modbus-slot cap), using holding `1002/1003`. Other WARP charging slots and cable/device limits remain authoritative. WARP3 Smart has no integrated meter; no power/session meter values are invented.

The current mapping follows Tinkerforge `esp32-firmware` revision `05762aabb8213219d63f8a52fe998f7e091edb2e`, particularly `modbus_tcp/modbus_reader.py`, `modbus_tcp/modbus_tcp.cpp` and `evse_common/evse_common.cpp`. Optional input 10/11 identity traces to the manufacturer-provisioned eFuse UID at [upstream revision cb474182](https://github.com/Tinkerforge/esp32-firmware/tree/cb4741826011d81895660961020d655f0e1c5648); it is diagnostic metadata and is never read as an authorization condition.

Marstek Venus E 3.0 V148 / Micro V119: existing curated acquire, charge/discharge power, stop, target SoC and release plans are unchanged. No stable identity is required; `release_replay_safe=false` is accepted for normal commands. No new register is invented in this package.

Marstek lifecycle behavior still needs physical validation on V148. Community reports of force-command watchdog/timeout behavior are **unvalidated for V148**; firmware assumes no automatic release timeout and does not depend on one. Independently resetting/disconnecting the device is an accepted operational recovery path. Check acquire/release, command persistence, power sign/scale and SoC against the actual device this weekend.

Native tests and CI validate software paths, not physical charging/discharging or firmware-specific register behavior.


## Economic policy boundary

Practical [storage/economic policies](HEMS_Policies.md) constrain the read-only DP.
The separate [economic dispatcher](HEMS_Economic_Dispatch.md) can consume its
current interval only after explicit default-off opt-in, Active, Storage HEMS
Control and every current plan/readiness/constraint gate. Manual storage commands
persist a pause; authenticated explicit Resume requires fresh replanning. Economic
idle uses semantic Stop, while cleanup Release relinquishes only economic
ownership. Hardware safety and EV Protection remain authoritative. The same
provider generation, depth-one mailbox, worker, profile plans and per-write
semantic executor checks handle every command. No implicit override timer exists.
