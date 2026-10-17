# OpenDTU-HEMS device profiles

This directory is a user-facing catalog of downloadable Modbus profiles for OpenDTU-HEMS.

These files are **not built into the firmware** and are **not installed automatically**. OpenDTU-HEMS ships with an empty runtime profile store. Download the JSON file for the device you use and import it through the HEMS profile administration UI.

Directory layout:

- `storage/` - battery and AC/DC storage systems
- `evse/` - EV chargers / wallboxes

Profile status is intentionally conservative:

- Vendor documented: values come from the manufacturer's own documentation.
- Target validated: values were checked against a specific real device/firmware target and cross-checked against independent public mappings.
- Community validated: values come from public mappings with real-device validation.
- Experimental: useful mapping exists, but important semantics still need broader hardware validation.

Some devices change Modbus addresses, scaling or semantics between firmware revisions. A profile therefore documents the source/target revision it was built against instead of pretending that one mapping is universally correct.

## Schema versions

`opendtu-hems-modbus-profile/v1` is telemetry-only and remains read-only.

`opendtu-hems-modbus-profile/v2` keeps the same telemetry model and may additionally declare a bounded `control` block. Control metadata is data, not executable code: it can describe optional acquire/release sequences and named operations made from fixed holding-register writes or a validated numeric operation input. Profiles cannot contain scripts, expressions or arbitrary code.

Importing a v2 profile with control metadata **does not grant HEMS permission to write to the device**. The profile only describes what the device supports and how a supported operation maps to Modbus. Write permission belongs to the Storage or EVSE device type, and the HEMS policy/control layer decides whether and when an allowed operation is requested.

This separation deliberately supports setups such as a WARP wallbox being used in monitor-only mode while its charging state influences storage policy, for example blocking battery discharge while the vehicle is charging.

The Marstek Venus E 3.0 V148 / Micro V119 reference contains curated acquire, charge/discharge, stop, target SoC and release operations. Register `30006` is normalized so charging is positive and discharging negative. The current `34002` SoC scale is `0.1`; confirm power/SoC plausibility against the physical firmware before interpreting it. Public Venus maps are firmware-dependent. No new register or assumed watchdog is introduced by the real-control package.

The Tinkerforge WARP3 Smart profile uses the manufacturer's Modbus implementation for telemetry and optional charging-current control metadata. Whether OpenDTU-HEMS may actually use those writes remains a per-device setting, independent of the profile contents.

A profile being present in this catalog does not mean every firmware consumer can already bind to it. Compatible `storage` profiles can be selected by the generic storage consumer. Compatible `evse` profiles can be selected by the generic EVSE consumer; an empty EVSE binding keeps the legacy WARP telemetry path.

The repository catalog is deliberately decoupled from firmware releases: profile files can be reviewed and updated without baking vendor mappings into the binary. Users remain in control of which profiles they import into their own OpenDTU-HEMS instance.

No profile in this directory may add executable write logic.


### Normal control and optional metadata

Manual control requires device-type HEMS control permission and normal profile/reachability/freshness readiness. Automatic policy writes also require global Active. Identity/recovery metadata is optional diagnostic information; WARP's eFuse identity and both profiles' `release_replay_safe=false` never block normal control. Marstek does not need stable identity. Watchdog behavior reported by the community remains unvalidated for V148 and is not an actuation prerequisite.

Import the current JSON explicitly. In Devices / Profiles, select a file with the same ID and leave replacement enabled to update the runtime copy. Capability changes are previewed; bound providers reload without firmware reboot after validation. Old schema-v1 or control-metadata-free runtime copies show a concrete explanation and remain readable until replaced.
