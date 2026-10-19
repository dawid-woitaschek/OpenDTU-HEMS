# OpenDTU-HEMS device profiles

This directory is a user-facing catalog of downloadable Modbus profiles for OpenDTU-HEMS.

These files are **not built into the firmware** and are **not installed automatically**. OpenDTU-HEMS ships with an empty runtime profile store. Open HEMS → Devices → Repository profiles and import the selected profile. Manual file import remains available as a fallback.

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

## Live repository catalog

The canonical profile source is the permanent `integration/hems-next` ref in
`dawid-woitaschek/OpenDTU-HEMS`. On changes to that ref, `profile-catalog.yml`
validates **every** category JSON with the production firmware parser, then
publishes a generated `catalog.json` on the `device-profile-catalog` distribution
branch. Nothing edits that manifest by hand. Adds, edits and deletions are reflected
in each generated snapshot. PRs validate generation and drift before publication;
profile-only changes do not require the firmware build. Until the first canonical
merge/workflow run, the UI reports that the catalog is not yet published.

The version-1 manifest contains `source_commit` and lightweight entries: ID,
category, vendor/model, schema, source revision, relative path, byte size, raw-file
SHA-256 and `installed_sha256` of the production parser's normalized JSON. No full
profiles are embedded. No wall-clock field is used, making a source snapshot
reproducible. Generate locally:

```sh
g++ -std=c++17 -Iinclude -Iwork/ArduinoJson/src pio-scripts/profile_catalog_validator.cpp src/hems/profiles/Profile.cpp -o work/profile-catalog-validator
python3 pio-scripts/create_profile_catalog.py --validator work/profile-catalog-validator
python3 pio-scripts/create_profile_catalog.py --validator work/profile-catalog-validator --check
python3 test/test_profile_catalog_generation.py
```

The browser makes one catalog GET to the fixed official raw GitHub origin. Import
makes one selected-profile GET pinned to the catalog's immutable `source_commit`,
checks exact byte size and SHA-256 (also on local HTTP pages), and sends the JSON
to authenticated `POST /api/hems/profiles/import`. The backend remains authoritative
for schema, bounds, operation metadata, replacement and in-use compatibility. It
stores no remote catalog. Installed summaries expose a digest of the normalized
stored file, allowing current/different comparison without downloading all local
or remote profiles. Older backends without a digest show comparison unavailable.

An installed ID requires explicit Update / Replace and confirmation. Successful
imports refresh the installed list and storage/EVSE selectors; existing backend
reload behavior updates bound consumers without reboot. No permissions change.
Repository, selected-file, schema and integrity failures are separate visible
states. Installed management and manual import stay available during outages.
Catalogs are bounded to 256 entries/256 KiB; profiles remain bounded to 6144 bytes.

Historical `docs/DeviceProfiles/` files describe DTU build/hardware configurations;
they are unrelated to this HEMS Modbus catalog.
