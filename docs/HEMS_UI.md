# HEMS Web UI structure

HEMS is a first-class subsystem of OpenDTU-HEMS rather than an entry scattered across the inherited Settings and Info menus.

Current routes:

- `/hems` - HEMS status/overview
- `/hems/settings` - general HEMS configuration
- `/hems/devices` - HEMS devices and Modbus profile administration

Legacy `/info/hems` and `/settings/hems` routes remain aliases for compatibility. `/hems/profiles` is also kept as an alias of the device/profile page while the PR #5 UI evolves.

Device/profile administration is deliberately separate from the general HEMS configuration form. Device configuration is saved inside its own card, while profile import/export/delete operations use their own API and must not visually appear to be committed by an unrelated Save button.

PR #5 adds generic storage telemetry to this device area without coupling the storage provider to any manufacturer.

User-facing cards describe HEMS device categories, not provider implementation names. The current WARP Modbus TCP integration is therefore shown as `Wallbox` / `EV charger`; WARP remains the concrete read-only provider underneath. Storage is shown as `Storage` / `Speicher` independently of the first Marstek reference profile.

Profile selectors are capability-scoped. The storage selector only lists imported profiles that explicitly declare the `storage` capability and also contain telemetry understood by the generic storage provider. A profile referenced by a consumer cannot be deleted until that assignment is removed. Missing persisted references are rendered as explicit configuration errors rather than as an unexplained empty dropdown.

If no compatible storage profile is available, the selector states that condition and the UI prevents enabling storage telemetry without a profile. These checks are convenience and diagnostics only; the backend independently enforces profile existence, consumer compatibility and in-use deletion protection.
