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
