# OpenDTU-OnBattery Overscaling Calculator Tests

This directory contains unit tests for the `OverscalingCalculator` class.

## Building and Running Tests

```bash
# Build and run all tests
make test

# Build only
make all

# Clean up
make clean
```

`make test` also checks CMT2300A receive-state detection using the production
wrapper with register I/O seams: stale preamble consumption, sync/CRC/packet
priority and preservation of receive flags. This does not validate RF reception
or hopping timing on hardware.

## Test Coverage

The tests cover various overscaling scenarios:
- No shading scenarios
- Single and multiple MPPT shading
- Increasing and decreasing power scenarios
- Realistic partial shading with non-zero values
- Edge cases and boundary conditions

## GitHub Workflow

Tests run automatically on GitHub when test files or the OverscalingCalculator are modified.

## HEMS WARP tests

`make test` also runs deterministic WARP decode/state fixtures and the production
Modbus socket client against native loopback TCP, using small platform/DNS shims.
These tests do not validate physical WARP, ESP32 timing or W5500 networking.
See [HEMS_WARP.md](../docs/HEMS_WARP.md) for coverage and the real-hardware gate.

After PlatformIO dependencies are installed, run the production configuration
JSON helper tests from the repository root:

```bash
python test/test_hems_config.py .pio/libdeps/generic_esp32s3_usb/ArduinoJson/src
```

## HEMS profile tests

Profile tests compile the production parser, decoder and transaction model with
ArduinoJson 7.4.3 (the existing firmware dependency). After a representative
PlatformIO build, `make test` uses its installed headers by default. For a
standalone native checkout, obtain that pinned dependency and run:

```bash
make test ARDUINOJSON=/path/to/ArduinoJson/src
```

CI checks out the same `v7.4.3` version before `make test`. Store tests use an
in-memory backend with injected commit failure; real LittleFS/power-cut and heap
behavior remain hardware validation. Semantic writes use fake register I/O; no physical device activation is exercised.


Focused real-control checks (no local firmware matrix):

```sh
make -C test test_provider_cleanup test_semantic_actuators test_control_readiness test_ev_protection test_profile_catalog ARDUINOJSON=../work/ArduinoJson/src
(cd test && ./test_provider_cleanup && ./test_semantic_actuators && ./test_control_readiness && ./test_ev_protection && ./test_profile_catalog)
python3 test/test_hems_config.py work/ArduinoJson/src
```

`test_provider_cleanup` compiles the production controller, policy, both provider workers, mailbox, planner and executor. Only external grid/PV inputs, tasks, configuration persistence, profile loading and wire I/O are deterministic seams. It covers all four permission combinations, Monitor manual commands, Active automatic gating/EV protection, debounce, transient failures, one-shot failed release, configuration generation races and same-ID capability reload. Identity/replay metadata is deliberately absent or ineligible and does not prevent control. Parser/catalog and separate semantic/readback tests validate bounded operations. Physical register/charging behavior remains a hardware test.

## Tariff / forecast and Admin regression coverage

`make test ARDUINOJSON=../work/ArduinoJson/src` includes `test_energy` and
`test_energy_service`: production bounded parser/provider/runtime, deterministic
UTC and monotonic clocks, local task/network/file seams, gaps, stale/error and
recovery, atomic settings persistence/reload and configuration generations.
`test_semantic_actuators` now loads the actual catalog profiles; `test_storage`
decodes Marstek profile fixtures with the declared positive-charge/negative-
discharge convention. These fixtures do not physically validate V148 mappings.

After webapp dependencies are installed, run `node test/test_hems_admin.mjs`
from the repository root. This executes the real Admin setup script and shared
response handler with fake fetches: successful old/new EVSE responses, release
warning, saved-but-reload-failed, POST/persistence failure and login expiry.
The reference HEMS CI build runs it as well. See [energy data](../docs/HEMS_Energy_Data.md).

`test_settings_stores` runs the production EVSE and storage settings stores with
the in-memory LittleFS seam, including failed persistence, durable unbinding,
disable/re-enable, explicit profile requirements and permission reload.

### HEMS product presentation

Run `yarn --cwd webapp test` after dependency installation. This retains Admin/auth
and tariff canonical-unit/save tests and adds deterministic calendar, classification,
price/forecast/freshness/provider-message and en/de structural checks.

For rendered hardware-free evidence, install Playwright into a scratch directory
(e.g. `npm install --prefix work/browser playwright`), start
`yarn --cwd webapp dev --host 127.0.0.1`, then run from the repository root:

```sh
PLAYWRIGHT_MODULE="$PWD/work/browser/node_modules/playwright" node test/test_hems_browser.mjs
```

The default Chromium path is `/usr/bin/chromium`; override with `CHROMIUM_PATH`.
Screenshots go to `work/screenshots` (override `HEMS_SCREENSHOTS`). Fixtures intercept
all API requests and include a DST fall-back day, six PV planes, effective/source
prices, battery/EV state and a Shadow schedule. No real devices/providers or commands
are involved. Browser tooling is optional and adds no firmware/runtime dependency.

### Storage/economic policies

`test_shadow_optimizer` and `test_optimizer_service` now also cover the
[Shadow policy contract](../docs/HEMS_Policies.md): legacy config defaults,
atomic persistence/reboot, permissions, bounds, exact dated targets,
infeasibility/coverage, PV substitution, numeric-price/category independence,
current protection/power constraints and terminal stock. The production service
still links to a controller exposing only read-only methods.

`test_hems_presentation.mjs` exercises one-time local/UTC conversion, DST gaps and
locale parity. The optional browser fixture saves and reloads policy permissions
and a dated target on mobile; all intercepted traffic still asserts zero device
commands. Socket/loopback tests require the environment's network permission.

### Economic storage dispatch

`test_economic_dispatch` exercises current-slot mapping, profile resolution, idle,
authorization gates, idempotence, ownership, source-specific cleanup, next-slot
transition and bounded retries. `test_economic_integration` runs the real optimizer,
controller, providers, mailbox, semantic plans and executor on deterministic wire/
filesystem/clock/task seams. It covers opt-in/default/Monitor/permission isolation,
charge/discharge readback, tariff/PV revisions, significant SoC changes, no repeated
DP/writes for unchanged inputs, durable manual pause/Resume/reboot, failed writes,
mid-command policy revocation and preserving actual EV Protection ownership on
economic disable. Existing control tests retain the explicit dispatch-off case.

`test_control_surface.py` guards the semantic-only, storage-only runtime and
authenticated Resume. Existing optimizer category-invariance, policy, TLS and
configuration suites remain. `test_economic_browser.mjs` uses the same optional
Playwright setup as the main browser suite, for opt-in, Monitor isolation, blocked
states, manual pause and explicit Resume at 320/360/768/1440px. These are
hardware-free fixtures; no physical battery or wallbox behavior is claimed.

### Firmware provenance and price/provider polish

`python3 test/test_firmware_provenance.py` runs the production PR/non-PR manifest
writer and compiled constant generator; `yarn --cwd webapp test` includes full-SHA
OTA/source comparison and legacy metadata recovery through the real System
Information method. See [firmware provenance](../docs/Firmware_Provenance.md).

Presentation tests cover the 2 ct/kWh robust-spread floor, isolated spikes,
negative/null prices, local/DST days, contiguous periods, visible Y ranges,
independent provider ages, manual-refresh rejection and display/control isolation.
Native tests additionally verify serialized freshness fields and retained tariff
coverage despite old source/transport timestamps. See
[presentation validation](../docs/HEMS_Presentation_Validation.md) for live network
limitations and remaining physical checks.

### Modernization regressions

`yarn --cwd webapp test` includes firmware lookup transitions/races and bounded
browser-catalog parsing/integrity tests. The generated-catalog Python tests reuse
the actual firmware parser (see `device-profiles/README.md` for host-tool setup).
`test_modernization_browser.mjs` exercises the rendered `pr-25 → deinemutter`
regression, late responses, old PR neutral wording, lazy import, backend rejection,
explicit replacement, selector refresh and repository outages at 360/1440 px.
It expects `work/profile-catalog/catalog.json` generated by the documented tool,
an isolated Playwright installation and a dev server on localhost:5173. The HEMS
browser suite additionally verifies named east-plane failures with retained data.
These are simulated browser/backend boundaries; physical runtime reload, TLS and
control timing still need ESP32 hardware validation.

Provider contract follow-up: `test_energy_charts_v2` exercises the strict uniform
v2 envelope, generation/fetch ages, integer money, metadata rejection, null/partial
horizons, 23/25-hour DST and retained planning data. `test_energy_transport` runs
the production bounded filter at 192/256 records and checks Retry-After holds.
Forecast.Solar sparse/dense capture fixtures are sanitized; the dense query is
retained deliberately and sparse overnight gaps are rejected atomically. Fixture
provenance is in `test/fixtures/providers/README.md`. No test contacts a provider.

### Full-HEMS runtime/memory stability

`test_hems_memory` compiles the production ESP32 placement path against a capability
allocator seam: PSRAM vs absent-PSRAM selection, exhaustion without INTERNAL spill,
and bounded JSON reallocation/accounting, including the expanded Plan and load
Profile types. `test_optimizer_service` checks learning-snapshot exhaustion retains
the published plan/pending request and recovers on retry. `test_energy_service` tracks live Series
across one/two/six-plane requests and delays, plus destination/aggregation allocation
failures with atomic last-good retention. `test_energy_transport` injects bad_alloc
in library constructors, begin/header/connect/body calls, checks RAII and subsequent
recovery, and proves unrelated exceptions propagate. Its clock/socket seams exercise
short reads, idle and total GET/body deadlines, millis wrap, exact payload limits,
truncation, trailing bytes, Content-Length isolation and bounded bulk socket reads.
`test_runtime_diagnostics` covers PSRAM history/serialization and cached failure
samples, including absent-PSRAM zeros. These tests do not measure ESP32 stability;
see [runtime memory stability](../docs/HEMS_Runtime_Memory_Stability.md).

### Coordinated energy management

`test_energy_management` exercises production flow decisions, DP economics,
terminal valuation/replanning, load learning, DST and deterministic forecast-error
and WARP-style reference comparisons. `test_optimizer_service` additionally
checks six-hour verified history checkpoints, failed rename, reboot and corrupted
history; `test_economic_integration` checks coordinated manual/worker revocation,
stale meter and unavailable DPL. All remain hardware-free. See
[behavior, configuration, measured resources and remaining hardware facts](../docs/HEMS_Energy_Management.md).
`yarn --cwd webapp test` includes explicit preset/opt-in and en/de coverage.


### Redesign UX/IA regression

`node test/test_webui_state.mjs` exercises shared live-connection ownership,
partial snapshots, refresh/frame ordering, last-known data, stale-owner isolation
and the strict EN/DE locale policy. `node test/test_redesign_browser.mjs` audits primary routes in
English/German at 320/768/1440 px, nested sidebar selection, physical-device
ownership, help placement/touch/outside/Escape behavior, connection status and
unchanged planning payloads. It is included in the HEMS browser regression job.

Browser suites accept `HEMS_BASE_URL` (default `http://127.0.0.1:5173`) so the
same flows can target a served production build. `PLAYWRIGHT_MODULE` and
`CHROMIUM_PATH` select an isolated browser installation. All device/provider
requests in these suites are intercepted; they do not validate physical hardware.

For firmware asset delivery checks, build the WebUI and run
`python3 test/fixtures/embedded_webapp_server.py`, then set
`HEMS_BASE_URL=http://127.0.0.1:5174` for the browser suite. This server mirrors
the firmware's fixed embedded-file routes, gzip responses and HTML fallback.
Serving every file in `webapp_dist` hides missing firmware asset routes.
The header regression requires a bundled SVG data URL, so a standalone logo
request cannot silently pass even when running against Vite.
