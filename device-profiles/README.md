# OpenDTU-HEMS device profiles

This directory is a user-facing catalog of downloadable Modbus profiles for OpenDTU-HEMS.

These files are **not built into the firmware** and are **not installed automatically**. Download the JSON file for the device you use and import it through the HEMS profile administration UI.

Directory layout:

- `storage/` - battery and AC/DC storage systems
- `evse/` - EV chargers / wallboxes

Profile status is intentionally conservative:

- Vendor documented: values come from the manufacturer's own documentation.
- Community validated: values come from a pinned community mapping with real-device validation.
- Experimental: useful mapping exists, but important semantics still need broader hardware validation.

A profile being present in this catalog does not mean every firmware consumer can already bind to it. The current generic storage consumer can use compatible `storage` profiles. The current WARP runtime integration still uses its dedicated read-only provider; the WARP3 profile is therefore importable/reference data today, not a replacement for that provider yet.

No profile in this directory may add executable write logic. Modbus profile schema v1 remains read-only.
