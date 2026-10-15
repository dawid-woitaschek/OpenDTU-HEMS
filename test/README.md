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
behavior remain hardware validation. No writes or device activation are exercised.


Focused recovery/session checks (no full firmware build):

```sh
make -C test test_recovery test_flash_journal test_provider_cleanup test_validation_session ARDUINOJSON=../work/ArduinoJson/src
(cd test && ./test_recovery && ./test_flash_journal && ./test_provider_cleanup && ./test_validation_session)
```

`test_flash_journal` exercises the production VFS adapter using native files under
`work/recovery-flash` and faulted write/fsync/rename calls. Identity fixtures are
synthetic; only the catalog test asserts documented real profile metadata.
Hardware reboot/replay/API validation remains a separate gate.
