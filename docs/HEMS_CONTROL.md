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

The command mailbox is now connected to the existing WARP and storage provider workers. There is still no public HTTP write endpoint and no policy caller. `Controller::submitActuation()` is the only controller-side handoff and it rejects every command while the global Active prerequisite is absent. The workers independently call back into the same readiness gate before opening a control session and the register executor rechecks authorization before every bounded write.

A provider configuration change cancels pending work and advances the provider generation. A running command cannot be synchronously cancelled, but subsequent writes fail their generation/authorization check. The worker follows a command with a fresh telemetry poll so observed state is refreshed promptly. Commands and telemetry share the same provider worker and Modbus client, preserving the one-device/one-owner rule rather than introducing a second network writer.

The mailbox never overwrites a pending or running command and it does not accumulate stale power requests.

Before a future Active mode can be enabled, shutdown/release semantics still need an explicit lifecycle policy. In particular, revoking Active or changing an endpoint while remote ownership may exist must define whether to stop, release, or both. This document does not pretend that problem vanishes merely because the write path is bounded.

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

Profiles such as the curated Venus E3 may define `acquire` and `release` write sequences. `ProfileSession` validates the requested operation before taking ownership, acquires once, retains ownership across subsequent operations and only clears local ownership after a successful release. An explicit forced release remains possible after local state was lost, for example following a reboot.

Physical Venus E3 behavior remains a hardware validation gate; profile metadata and native tests do not claim otherwise.
