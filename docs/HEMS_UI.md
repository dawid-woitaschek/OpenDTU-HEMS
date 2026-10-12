# HEMS Web UI structure

HEMS is a first-class subsystem of OpenDTU-HEMS rather than an entry scattered across the inherited Settings and Info menus.

Current routes:

- `/hems` - HEMS status/overview
- `/hems/settings` - general HEMS configuration
- `/hems/devices` - HEMS devices and Modbus profile administration

Legacy `/info/hems` and `/settings/hems` routes remain aliases for compatibility. `/hems/profiles` is also kept as an alias of the device/profile page while the PR #5 UI evolves.

Device/profile administration is deliberately separate from the general HEMS configuration form. Device configuration is saved inside its own card, while profile import/export/delete operations use their own API and must not visually appear to be committed by an unrelated Save button.

PR #5 adds generic storage telemetry to this device area without coupling the storage provider to any manufacturer.
