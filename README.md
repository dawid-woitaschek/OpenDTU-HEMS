# OpenDTU-HEMS

> [!WARNING]
> **Experimental project.** HEMS features are implemented, but physical validation is incomplete. Do not rely on OpenDTU-HEMS for unattended or safety-critical energy control. Device and BMS protections remain authoritative.

OpenDTU-HEMS is a local-first Home Energy Management System based on
[OpenDTU-OnBattery](https://github.com/hoylabs/OpenDTU-OnBattery).
**`hems` is the product and default branch.** It retains inverter communication,
battery/BMS integration, power meters and the Dynamic Power Limiter (DPL), with
supervisory energy management that runs locally without mandatory cloud services
or Home Assistant. Native tariff/forecast providers use external services;
local/JSON inputs and MQTT integration are also available.

## Implemented today

- Disabled / Monitor / Active modes, independent Storage and EVSE control permissions,
  and authenticated manual semantic commands through bounded device profiles.
- WARP3 telemetry/current control and Marstek storage telemetry/control paths;
  Active EV Protection blocks storage discharge while the EV is charging.
- Exact-price storage optimization, optional PV forecasts, reserve/target policies
  and **default-off, opt-in** economic storage dispatch. Manual storage commands
  pause economic automation until explicit Resume.
- Energy-Charts DE-LU v2 pricing, Forecast.Solar, verified repository profile import,
  storage-plan presentation and on-demand redacted support snapshots with heap history.
- Coordinated DPL/storage flows, independent export permissions, optional household-load
  learning, battery-wear costs, terminal inventory valuation and monetary hysteresis
  in the bounded global DP (merged PR #34; coordination and learning default off).
- Runtime/PSRAM hardening and refreshed CMT2300A radio recovery/reception work.

**Implemented does not mean physically validated.** PR #35's approximately 21-hour
Device A run with 2 MB PSRAM is accepted for the current runtime-hardening stage;
successful Forecast.Solar TLS cycles are recorded. WARP3/Marstek control lifecycles,
EV Protection, physical economic charge/discharge, representative Energy-Charts
ESP32 runs and the refreshed CMT behavior still need hardware validation.
PR #34 energy management is merged; physical optimizer, coordination and dispatch
validation remains pending on the combined PSRAM build.
Economic EV Charging is deferred; PR #33 preserves its shadow-planning design/history.

## Hardware and getting started

**PSRAM is the expected Full-HEMS memory tier.** The runtime reference is Device A:
Fusion Board v2.1, ESP32-S3, 8 MB flash, 2 MB quad PSRAM,
`generic_esp32s3_usb_quad_psram` (Shield v1.1 for control validation).
No-PSRAM builds are compatibility targets only; historical successful runs do not
establish Full-HEMS stability without PSRAM. Select the build matching the board's
actual PSRAM type; see [runtime memory stability](docs/HEMS_Runtime_Memory_Stability.md).

Use the upstream [hardware](https://opendtu-onbattery.net/hardware/),
[installation](https://opendtu-onbattery.net/firmware/) and
[base configuration](https://opendtu-onbattery.net/firmware/device_profiles/)
guides for inherited functionality. For HEMS, start in Monitor with economic
dispatch off and device permissions Monitor only; import and bind the appropriate
profiles before enabling control. Follow the [hardware validation runbook](docs/HEMS_Hardware_Validation.md)
for supervised control tests and firmware identity/normal OTA checks.

## HEMS documentation

- [Current status and validation evidence](docs/HEMS_Current_Status.md) · [Roadmap](docs/HEMS_Roadmap.md)
- [Architecture](docs/HEMS_Architecture.md) · [Control model](docs/HEMS_CONTROL.md) · [Device profiles](docs/HEMS_Modbus_Profiles.md)
- [Energy data](docs/HEMS_Energy_Data.md) · [Native providers and optimizer](docs/HEMS_Native_Energy.md) · [Storage policies](docs/HEMS_Policies.md) · [Economic dispatch](docs/HEMS_Economic_Dispatch.md)
- [UI](docs/HEMS_UI.md) · [Development and local tests](test/README.md)

## Inherited compatibility notes

- **Avoid Hoymiles inverter firmware 2.0.4:** its temporary-limit reporting can break
  DPL operation ([upstream issue #1901](https://github.com/hoylabs/OpenDTU-OnBattery/issues/1901)).
  Upstream recommends 1.1.12; 1.0.x lacks Power Distribution Logic.
- ESP32 boards with 4 MB flash do not support OTA in the inherited OnBattery layout.
  See the [8 MB upgrade guide](https://opendtu-onbattery.net/firmware/howto/upgrade_8mb/#background).
- Build the web application before compiling firmware; see the
  [build instructions](.github/copilot-instructions.md) and upstream
  [webapp compilation guide](https://opendtu-onbattery.net/firmware/compile_webapp/).

## Upstream and acknowledgments

Thanks to Thomas Basler ([@tbnobody](https://github.com/tbnobody)) and the
[OpenDTU](https://github.com/tbnobody/OpenDTU) contributors, [@helgeerbe](https://github.com/helgeerbe),
the `hoylabs` maintainers and all OpenDTU-OnBattery contributors.
The upstream [documentation](https://opendtu-onbattery.net),
[wiki](https://github.com/hoylabs/OpenDTU-OnBattery/wiki) and
[releases](https://github.com/hoylabs/OpenDTU-OnBattery/releases) cover the base project.
OpenDTU-HEMS retains the [license](LICENSE) inherited from upstream.
