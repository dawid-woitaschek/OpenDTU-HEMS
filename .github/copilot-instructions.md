# OpenDTU-HEMS GitHub Copilot Instructions

OpenDTU-HEMS is ESP32 C++ firmware with an embedded Vue/TypeScript web application,
based on OpenDTU-OnBattery. `hems` is the product and default branch. Preserve the
inherited inverter/BMS/meter/Dynamic Power Limiter behavior while developing the
local-first HEMS layer. HEMS is experimental supervisory automation; device and
BMS protections remain authoritative.

## Binding HEMS project-state maintenance

`docs/HEMS_Roadmap.md` and `docs/HEMS_Current_Status.md` are the authoritative
project-state documents.

- **Every HEMS-relevant PR must review both files and, when project state changes,
  update both in the same PR.** This includes implementation, validation, hardware
  status, open work, branches/PRs, firmware channels and known risks.
- Before merge or promotion to `hems`, ensure both describe the resulting product
  state accurately and remove stale information. Do not copy obsolete branch or
  temporary PR-channel instructions into current documentation.
- Distinguish implemented software, local checks, CI results and physical evidence.
  Explicitly label unmerged and physically unvalidated work. **Never claim CI,
  merge or hardware validation that did not actually happen.** Historical checks
  are evidence for their recorded revision, not checks run by a later PR.
- Use focused local checks appropriate to the change. Do not wait for or poll
  remote CI / GitHub Actions.

Read [Architecture](../docs/HEMS_Architecture.md), [Control](../docs/HEMS_CONTROL.md),
[Energy data](../docs/HEMS_Energy_Data.md) and [Native providers/optimizer](../docs/HEMS_Native_Energy.md)
for contracts. Manual and automatic control share semantic actuators, profile bounds,
permissions, generation/freshness gates and existing provider workers/mailboxes.
Monitor permits authorized manual commands but no automatic writes. Economic storage
dispatch is a separate default-off opt-in; manual storage control durably pauses it
until explicit Resume. Do not introduce raw-register write APIs or competing writers.

## Hardware and memory target

**PSRAM is the expected Full-HEMS memory tier.** The current runtime reference is
Device A: Fusion Board v2.1, ESP32-S3, 8 MB flash, 2 MB quad PSRAM,
`generic_esp32s3_usb_quad_psram`. Shield v1.1 is the control-validation reference.
Match quad/octal PSRAM environments to actual hardware. Existing no-PSRAM targets,
including the PlatformIO default `generic_esp32s3_usb`, are compatibility builds,
not the Full-HEMS design/acceptance target.

Keep bounded allocation, INTERNAL/PSRAM diagnostics, verified TLS and existing
worker ownership. The accepted approximately 21-hour PR #35 Device A run does not
validate every scenario or later changes. Host tests, builds and register readback
do not prove physical power flow, watchdog behavior or RF reliability. Follow
[Runtime memory stability](../docs/HEMS_Runtime_Memory_Stability.md) and the
[hardware runbook](../docs/HEMS_Hardware_Validation.md) for evidence boundaries.

## Setup and build

Use the repository's Node.js 26 toolchain and pinned Yarn version from
`webapp/package.json`; use Corepack rather than an unrelated global Yarn version.
The workflows and `platformio.ini` define the current versions and environments.

```bash
# If the toolchain is not already installed:
curl -fsSL https://deb.nodesource.com/setup_26.x | sudo -E bash -
sudo apt-get install nodejs -y
node --version  # v26.x
npm install --global corepack@0.36.0
corepack enable
python3 -m pip install -r pio-scripts/requirements.txt
python3 -m pip install cpplint

# From the repository root, build web assets before firmware:
cd webapp
yarn install --immutable
yarn build
cd ..
pio run -e generic_esp32s3_usb_quad_psram
```

The firmware embeds `webapp_dist/`. `pio-scripts/compile_webapp.py` can rebuild
assets when needed. First firmware builds may take tens of minutes while downloading
the pinned ESP32 platform, libraries and tools; allow adequate command time.
Network/download failures are build-environment limitations, not passing validation.
Webapp installation also needs network access unless dependencies are cached.

Build environments include `generic_esp32_4mb_no_ota`, `generic_esp32_8mb`,
`generic_esp32s3`, `generic_esp32s3_usb`, `generic_esp32s3_usb_quad_psram` and
`generic_esp32s3_usb_octal_psram`; consult `platformio.ini` for the full list.

## Local validation

Choose checks for the changed paths; report exactly what ran and any limitations.
For frontend work:

```bash
cd webapp
yarn test
yarn type-check
yarn lint                         # Applies fixes; review the diff afterward
yarn prettier --check src/
yarn build
```

For a focused edit, ESLint/Prettier can target the changed component directly.
For C++ changes, use the relevant native tests and repository lint filters:

```bash
cpplint --repository=. --recursive \
  --filter=-build/c++11,-runtime/references,-readability/braces,-whitespace,-legal,-build/include \
  ./src ./include ./lib/Hoymiles ./lib/MqttSubscribeParser ./lib/TimeoutHelper ./lib/ResetReason

# Obtain ArduinoJson v7.4.3 at work/ArduinoJson as in .github/workflows/test.yml:
make -C test test ARDUINOJSON=../work/ArduinoJson/src
git diff --check
```

[test/README.md](../test/README.md) documents individual native suites and rendered
browser regressions. Browser fixtures never contact physical devices. Build firmware
for affected environments when firmware changes warrant it; the full matrix and
publication workflows run independently. Before recommending a firmware image for
flashing, verify its source identity, target, size/SHA256 and normal OTA image;
never use a factory image for OTA.

## Layout and development

- `src/`, `include/`, `lib/`: firmware, headers and protocol libraries;
  `src/main.cpp` is the firmware entry point.
- `src/hems/`, `include/hems/`: HEMS control, providers, profiles, energy and optimizer.
- `device-profiles/`: repository-backed Modbus profiles.
- `webapp/src/`: Vue UI; `webapp/package.json` and `yarn.lock` define tooling.
- `pio-scripts/`, `platformio.ini`: firmware build/packaging.
- `docs/`, `test/`: HEMS contracts, state/evidence and deterministic regressions.
- `.github/workflows/`: firmware build, native tests, C++ lint, frontend lint/format.

Use `yarn dev` in `webapp/` for development or `yarn preview` for a production
preview on port 4173. If assets are missing, build the webapp first. If Yarn is
missing, enable Corepack; if Node is incompatible, select Node 26. Diagnose actual
PlatformIO download errors rather than changing firmware to hide network failures.
