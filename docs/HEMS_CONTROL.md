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

The command mailbox is connected to the existing WARP and storage provider workers. There is still no public HTTP write endpoint and no policy caller. `Controller::submitActuation()` is the only controller-side handoff and it rejects every command while the global Active prerequisite is absent. The workers independently call back into the same readiness gate before opening a control session and the register executor rechecks authorization before every bounded write.

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

A power loss/reboot can still erase RAM-only ownership knowledge. Boot recovery is blocked until device-instance identity can distinguish the original device from a replacement at the same address; neither current reference profile defines such identity reads. No automatic reboot release or recovery persistence exists. See milestone B in `HEMS_Roadmap.md`.

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
