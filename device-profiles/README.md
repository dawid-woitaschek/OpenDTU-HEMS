# OpenDTU-HEMS device profiles

This directory is a user-facing catalog of downloadable Modbus profiles for OpenDTU-HEMS.

These files are **not built into the firmware** and are **not installed automatically**. Download the JSON file for the device you use and import it through the HEMS profile administration UI.

Directory layout:

- `storage/` - battery and AC/DC storage systems
- `evse/` - EV chargers / wallboxes

Profile status is intentionally conservative:

- Vendor documented: values come from the manufacturer's own documentation.
- Target validated: values were checked against a specific real device/firmware target and cross-checked against independent public mappings.
- Community validated: values come from public mappings with real-device validation.
- Experimental: useful mapping exists, but important semantics still need broader hardware validation.

Some devices change Modbus addresses, scaling or semantics between firmware revisions. A profile therefore documents the source/target revision it was built against instead of pretending that one mapping is universally correct.

The Marstek Venus E 3.0 V148 profile is based on the target setup (Control V148 / Micro V119) and independent cross-checks from multiple public projects/forums rather than one community repository. On that target, register `34002` required scale `0.01`; using `0.1` produced a tenfold SoC error. Register `30006` is normalized to OpenDTU-HEMS storage semantics so charging is positive and discharging is negative. Public Venus E3 mappings disagree on SoC scaling, some addresses and even power-sign behavior, so unverified energy counters and control registers are deliberately omitted.

A profile being present in this catalog does not mean every firmware consumer can already bind to it. The current generic storage consumer can use compatible `storage` profiles. The current WARP runtime integration still uses its dedicated read-only provider; the WARP3 profile is therefore importable/reference data today, not a replacement for that provider yet.

No profile in this directory may add executable write logic. Modbus profile schema v1 remains read-only.
