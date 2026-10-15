# HEMS actuation foundation

This document describes the control foundation added after the original read-only HEMS milestones. It does not supersede the historical read-only validation notes in `HEMS_WARP.md`.

## Safety layering

Control is intentionally split into independent gates:

1. the device instance grants `HEMS control` instead of `Monitor only`;
2. the selected imported profile contains compatible schema-v2 control metadata;
3. the device is configured, enabled, reachable and has fresh telemetry;
4. device telemetry does not indicate a fault;
5. a future global HEMS `Active` mode authorizes actuation;
6. a depth-one worker mailbox hands a validated semantic command to exactly one device owner;
7. the semantic actuator resolves the command to a profile operation;
8. the profile planner validates/scales it to bounded holding-register writes;
9. the Modbus executor checks authorization immediately before every write, writes in order and verifies readback where applicable.

The current controller deliberately keeps item 5 false. `Readiness::readyForActive()` can therefore report that every device-side prerequisite is satisfied while `Readiness::allowed()` still remains false.

The command mailbox is connected to the existing WARP and storage provider workers. There is no public normal-operation control endpoint or automatic policy caller. The separate authenticated, temporary validation API is described below and cannot bypass recovery readiness. `Controller::submitActuation()` is the only controller-side handoff and it rejects every command while the global Active prerequisite is absent. The workers independently call back into the same readiness gate before opening a control session and the register executor rechecks authorization before every bounded write.

A provider configuration change cancels pending work and advances the provider generation. The accepted generation travels with a mailbox command, so work accepted for one endpoint can never be retargeted to another endpoint after a configuration change. A running command cannot be synchronously cancelled, but subsequent writes fail their generation/authorization check. The worker follows a command with a fresh telemetry poll so observed state is refreshed promptly. Commands, telemetry and cleanup share the same provider worker and Modbus client, preserving the one-device/one-owner rule rather than introducing a second network writer.

The mailbox never overwrites a pending or running command and it does not accumulate stale power requests.

### Ownership across configuration generations

A generation change is not allowed to silently erase local knowledge that the previous endpoint may still be under remote control. If an actuator still owns control when configuration changes, the provider retains the exact old endpoint, effective Modbus unit/port and the shared old profile object before installing the new generation.

Normal actuation then fails closed while the retained context exists. `Readiness` exposes `cleanup_pending`, so even a future global Active state cannot authorize new commands until cleanup has completed.

Cleanup is deliberately narrower than normal control:

- it may run after Active/device permission has already been revoked;
- it is tied to a private retained-context token;
- it can execute only the old profile's bounded `release` plan;
- it uses the same provider worker and Modbus client as telemetry/control;
- it never sends the release sequence to the new endpoint/profile;
- successful release clears local ownership and the retained context;
- failures keep the old context and retry 30 seconds after the failed attempt completes.

This exception to the normal authorization gate exists only to relinquish control safely. It cannot submit charge/discharge/current/stop operations and therefore must not be generalized into an alternate actuator path.

If a configuration change races with a running command, the command's mailbox envelope retains its original provider generation. If that command acquired ownership before later writes were revoked, completion retains the command's already-copied old endpoint/profile for the same cleanup path.

RAM-only retained cleanup is now backed by durable per-target ownership intent. Boot recovery is fail-closed unless the exact identity/replay requirements below hold. Both reference profiles still disable automatic release. See milestones B1–B4 in `HEMS_Roadmap.md`.

## WARP3 Smart control provenance

The WARP3 Smart schema-v2 profile uses Tinkerforge's WARP Modbus table rather than the deprecated compatibility register.

Official Tinkerforge firmware source confirms:

- holding register `1000` is `enable_charging_deprecated`;
- holding register `1002` is the 32-bit `allowed_current` value;
- writes to `1002` call `evse_common.set_modbus_current(...)`;
- that function updates the Modbus TCP charging-slot maximum current;
- charging-slot current semantics accept `0` or `6000..32000` mA, with values below 6000 considered inoperative;
- when Modbus control is enabled, Tinkerforge initializes the Modbus charging slot as active with a `32000` mA maximum current.

Relevant upstream source:

- `software/src/modules/modbus_tcp/modbus_reader.py`
- `software/src/modules/modbus_tcp/modbus_tcp.cpp`
- `software/src/modules/evse_common/evse_common.cpp`

Verified against Tinkerforge `esp32-firmware` revision `05762aabb8213219d63f8a52fe998f7e091edb2e`.

Therefore the OpenDTU-HEMS profile semantics are:

- `set_current_a(6..32)` -> holding `1002/1003`, uint32, raw mA (`6 A` -> `6000`);
- `stop_charging()` -> holding `1002/1003 = 0`;
- `releaseControl()` -> holding `1002/1003 = 32000`, restoring Tinkerforge's neutral/default Modbus-slot limit without changing the charger's Modbus-enable configuration.

`stop` and `release` deliberately have different meanings: stop imposes a zero-current HEMS limit, while release removes the HEMS-specific current cap and leaves the charger's other slots, cable limits and safety logic in charge.

This validates the profile metadata and semantic actuator mapping. It does not constitute a physical charger validation.

## Storage ownership

Profiles such as the curated Venus E3 may define `acquire` and `release` write sequences. `ProfileSession` validates the requested operation and a non-empty release plan before taking ownership. It acquires once, retains ownership across subsequent operations and only clears local ownership after a successful release. If an acquire write was attempted but its response/readback failed, ownership remains possible and further normal operations fail closed until release succeeds. A write rejected before the first acquire attempt does not establish local ownership. Empty release metadata cannot report successful cleanup; providers also report such profiles as not control-capable. Retained-context cleanup uses that same bounded release plan against the exact previous endpoint/profile after a configuration transition.

Physical Venus E3 behavior remains a hardware validation gate; profile metadata and native tests do not claim otherwise.


## Durable recovery protocol (B1/B2)

Each target has a committed binary record and a pending replacement file in the existing LittleFS mount (`/littlefs`). The wire record is fixed-size (515 bytes, bounded by 544), versioned, CRC32 checked, and explicitly encoded without native-struct padding. It stores only target, exact host/effective port/unit, profile ID, exact bounded recovery compatibility bytes, diagnostic generation, possible-ownership flag and actual captured identity. There is no telemetry, command history or wall-clock dependency. Compatibility pins schema/capability, the exact bounded release plan, replay eligibility and identity read layout. It is not physical device identity; comparison is byte-exact rather than hash-based authorization.

Ordering is deliberate:

1. validate semantic operation, acquire and nonempty bounded release;
2. recheck authorization; if a profile declares identity, read it through the open provider client;
3. write pending intent, check write/fsync/close, verify content, atomically rename over committed intent, verify namespace/content;
4. attempt acquire/ownership write and operation through the existing session/executor.

The VFS operations expose errors which Arduino `File.flush()/close()` cannot report. ESP LittleFS commits file sync and atomic rename metadata synchronously; no unlink-before-rename gap exists. Any pending file at boot is unresolved, even if empty or accompanied by a valid committed tombstone. Parsing/version/CRC/read errors never mean an empty journal. Generic file upload/delete and configuration reset preserve the reserved `hems-recovery-` namespace (including pending evidence); the normal file list excludes it. Admin forensic download is read-only. Mount failures preserve the filesystem; automatic boot formatting is disabled. Fresh hardware needs a provisioned LittleFS filesystem. Do not downgrade to firmware that ignores the journal, upload a filesystem image, erase flash, or factory-reset the controller while external ownership may persist; first independently resolve every controlled device.

After bounded release/readback succeeds, local ownership becomes released and a zeroed, versioned target-specific tombstone replaces the ownership record using the same durable protocol. Power loss before that replacement leaves stale intent, which is intentionally safe. A failed durable clear remains blocked; this boot can retry the known completed release's **flash clearing only**, never repeat a non-replay-safe release just because flash failed. After reboot, stale intent must pass the complete recovery policy again.

Provider workers prioritize recovery over new commands and continue telemetry when useful. Scheduler and network callbacks do not read/write journals or perform recovery Modbus/DNS work. Recovery attempts have a finite five-attempt budget and 30-second delay after failed-attempt completion, including client close. Unresolved states do not retry hot. Configuration changes cannot replace the saved recovery endpoint/profile. Live retained cleanup still releases the captured old context and clears the same journal.

`/api/hems/status` exposes each target's profile, common readiness blockers, ownership uncertainty, command errors/stages, cleanup state and recovery state/reason, intent presence, identity support/capture, automatic capability, attempts/retry delay, durable-clear state, record identifier and operator acknowledgement. All diagnostic structures are bounded RAM snapshots. No periodic diagnostic persistence occurs.

## Bounded device-instance identity and replay metadata

Schema v2 optionally accepts:

```json
{
  "identity": {
    "kind": "device_instance",
    "source": "Exact vendor documentation/source provenance",
    "fields": [
      { "space": "input", "address": 90, "count": 2, "encoding": "register_bytes" }
    ]
  },
  "recovery": { "release_replay_safe": false }
}
```

Address 90 above is a **synthetic example**, not a reference-device register. Identity allows 1–4 FC03/FC04 reads, 1–8 registers each, at most 64 bytes. The canonical identity is the exact wire bytes in declared field order; layout/count/address are pinned by compatibility. No floats, scale, truncation, scripting, expressions, trimming or probabilistic matching are available. All-zero/all-ones fields are rejected as invalid sentinel data. Firmware/table/model/profile/network identifiers are not stable instance identity.

`release_replay_safe` defaults false. Control capability and reboot-recovery capability are separate. Before any automatic reboot release, the worker requires valid intent, available compatible profile, identical endpoint, captured stable identity, a successful current identity read with exact match, an explicitly replay-safe valid release and a still-current generation gate. Failure of any static prerequisite yields unresolved/no write. Identity-read or transport failure stays blocked with bounded retry. Identity mismatch never writes.

### WARP3 identity provenance

Verified against upstream Tinkerforge `esp32-firmware` commit [`cb4741826011d81895660961020d655f0e1c5648`](https://github.com/Tinkerforge/esp32-firmware/tree/cb4741826011d81895660961020d655f0e1c5648):

- [`modbus_tcp/modbus_reader.py`](https://github.com/Tinkerforge/esp32-firmware/blob/cb4741826011d81895660961020d655f0e1c5648/software/src/modules/modbus_tcp/modbus_reader.py) labels input 10/11 as uint32 `box_id`;
- [`modbus_tcp/modbus_tcp.cpp`](https://github.com/Tinkerforge/esp32-firmware/blob/cb4741826011d81895660961020d655f0e1c5648/software/src/modules/modbus_tcp/modbus_tcp.cpp), `getWarpInputRegister` case 10, returns `esp32_common.get_uid_num()`;
- [`esp32_common.cpp`](https://github.com/Tinkerforge/esp32-firmware/blob/cb4741826011d81895660961020d655f0e1c5648/software/src/modules/esp32_common/esp32_common.cpp), `pre_setup`, reads `EFUSE.blk3_rdata7.val`;
- [`provision_common.py`](https://github.com/Tinkerforge/esp32-firmware/blob/cb4741826011d81895660961020d655f0e1c5648/provisioning/provision_common/provision_common.py), `handle_block3_fuses`, allocates/preserves the manufacturer UID and burns its four bytes at block-3 offset 28. It is controller hardware identity, independent of firmware version/network configuration.

The curated WARP profile now reads those two input registers. Replacement of the ESP32 controller changes the relevant instance. Actual installed-device UID stability and comparison remain a hardware test. Existing source makes a repeated neutral 32000-mA slot assignment plausible, but the reference profile deliberately retains replay-safe=false until actual stale-journal release/readback/reboot behavior is validated. No real automatic release is enabled by this package.

### Marstek identity provenance outcome

The repository's Venus V148/Micro V119 profile provenance is public cross-check/reverse-engineering material, not a vendor-validated immutable identity definition. The current Tinkerforge battery source tree inspected above has no Marstek/Venus identity mapping. Focused public repository discovery found community projects rather than attributable vendor firmware/register evidence for an immutable ID on this exact supported TCP path. No model/version/serial-like community register was promoted to instance identity. A vendor-backed stable identity read and validated replay semantics are still required; the profile declares neither identity nor replay-safe release.

## Independent operator resolution

This is an explicit attestation protocol, not a force-release or ignore-identity control. HEMS stays Disabled/Monitor and blocked throughout unresolved recovery. First independently clear/reset remote control on the **original physical device**, through its physical controls or vendor interface, and verify it is autonomous. Changing the DTU configuration, network address or profile does not establish this.

Admin-authenticated GET `/api/hems/recovery/record?target=evse|storage` returns the saved endpoint/profile, reason and exact current record ID from one locked snapshot. Compare this with the device actually reset. Record IDs use a fresh 128-bit random nonce per load/new intent; stale acknowledgements across reboot/new records are rejected. They are not physical device identity and cannot authorize a Modbus write.

Admin POST `/api/hems/recovery/resolve`, using the existing form `data` JSON convention (maximum 512 bytes), requires:

```json
{
  "target": "evse",
  "record_id": "exact current 32-character identifier",
  "independently_released": true,
  "attestation": "I independently cleared remote control on this physical device"
}
```

Only a valid unresolved ownership record can be acknowledged. The handler queues RAM state only (202); the existing worker logs the attestation, issues **no Modbus write**, and atomically clears intent. Normal actuation stays blocked until durable clearing succeeds. Status records operator acknowledgement and errors. A clear failure retries flash only and is still blocked after reboot. Missing/corrupt/unsupported records cannot be acknowledged by this API because their context is untrustworthy; preserve the filesystem/evidence, independently reset every possibly controlled device, and use a separate reviewed offline repair/provisioning procedure. There is no journal-delete button.

## Temporary hardware-validation sessions (C1)

All routes require existing admin credentials. No route enables global Active.

| Route | Behavior |
|---|---|
| POST `/api/hems/validation/session` | Start one server-side RAM session; default 120 s, `data: {"ttl_ms":...}` within 1–300 s; returns a 128-bit random token once |
| DELETE `/api/hems/validation/session` | Revoke matching `X-HEMS-Validation-Token`; request production retained cleanup |
| GET `/api/hems/validation/status` | Session expiry/state and bounded per-target readiness/command/cleanup/recovery diagnostics; no token disclosure |
| POST `/api/hems/validation/command` | Matching token header + bounded form `data` with target/action/optional value; one command per second; production depth-one worker handoff |

Semantic actions are `set_current_a`, `set_charge_power_w`, `set_discharge_power_w`, `set_target_soc_pct`, `stop`, `release`, restricted by target and the actual profile/actuator planner. Example synthetic request: `{"target":"storage","action":"set_charge_power_w","value":500}`. HTTP does not implement register writes or duplicate planner bounds. Input/range/scaling/readback validation stays in the production path; mailbox results report planning/transport/readback/verification errors separately.

Common readiness requires per-device HEMS permission, valid configured/enabled profile, fresh reachable telemetry, no fault/cleanup/recovery blocker, an active validation session **and automatic recovery capability**. Both real reference profiles currently fail `recovery_automatic_unsupported`. Framework/native readiness coverage therefore does not expose real ownership-producing hardware commands. Session creation alone does nothing to a device.

Every write rechecks server-side session/readiness/generation; expiry/revocation/config changes prevent new writes and cancel pending commands. Ownership created by a running command concurrently with revocation is retained at completion for the existing cleanup worker. Cleanup can run after permission/session expiry, only against the captured release context. Expiry with no ownership performs no filesystem operation. Tokens are never saved in browser storage; losing a token/reloading does not extend the TTL. The settings panel can start/revoke a session and shows countdown/blockers; real semantic control widgets remain deferred while reference recovery capability is blocked.

## Focused verification and limits

Native tests cover persist-before-first-write, preflight/authorization rejection, uncertain writes, acquire-less ownership, release/clear ordering, clear-only retry, reboot/corruption/truncation/version/incomplete IO, profile/endpoint/identity mismatches, missing identity, identity read failures, matching synthetic recovery, replay-disabled rejection, finite completion-based retry, common readiness, both production providers, config changes during identity reads, cleanup after running-command revocation, exact-record operator tokens, and snapshot concurrency. Production journal VFS tests fault short writes, fsync and atomic rename. Validation session tests cover random-token lifecycle, TTL/wraparound, stale tokens, revocation and rate limits.

The existing small DNS callbacks, worker ownership, DPL isolation and TCP/IP/AsyncTCP stack sizes are unchanged. Profile field layout was compacted to retain the existing 4-KiB profile bound (4008 native bytes including identity metadata). Journal records/identity reads/JSON requests are bounded. A focused native stack-usage review moved the two nested 544-byte journal serialization/verification buffers into checked, fixed-size transient heap allocations; worker and network task stack sizes were not increased. No full PlatformIO build, full local CI reproduction or full lint suite is part of this work block; GitHub CI is authoritative. Hardware power-cut testing, reference-device write/replay validation and actual API/UI firmware smoke tests remain required.
