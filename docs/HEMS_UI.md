# HEMS Web UI structure

HEMS is a first-class subsystem of OpenDTU-HEMS rather than an entry scattered across the inherited Settings and Info menus.

Current routes:

- `/hems` - HEMS status/overview
- `/hems/settings` - HEMS configuration
- `/hems/profiles` - Modbus profile administration

Legacy `/info/hems` and `/settings/hems` routes remain aliases for compatibility.

The profile administration is deliberately separate from the HEMS configuration form. Profile import/export/delete operations use their own API and must not visually appear to be committed by the normal HEMS Save button.

As PR #5 adds generic storage telemetry, the profile page can evolve into device/profile administration without coupling the storage provider to any manufacturer.
