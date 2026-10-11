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
