# Tests

> 日本語版: [README.ja.md](README.ja.md)

This directory contains automated tests for EspUsbHost. Tests run on real ESP32 hardware via [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) with the Arduino CLI backend.

## Prerequisites

- [uv](https://docs.astral.sh/uv/) — Python package and environment manager
- [Arduino CLI](https://arduino.github.io/arduino-cli/) — used internally by pytest-embedded to build and flash sketches
- The target boards connected to the host PC via USB (for programming and Serial monitor)

## Setup

Copy the example environment file and edit it to match your serial ports:

```sh
cp .env.example .env
```

`.env.example` contains:

```sh
# Serial ports for each board profile
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB0
TEST_SERIAL_PORT_S3_HUB_HOST=/dev/ttyACM1

# Optional: generate an HTML report
#PYTEST_ADDOPTS="--html=report.html --self-contained-html"
```

Set each `TEST_SERIAL_PORT_*` variable to the actual serial port for the corresponding board.

## Running tests

From the `tests/` directory:

```sh
# Run all tests
uv run --env-file .env pytest

# Run only the peer tests
uv run --env-file .env pytest peer/

# Run a specific test
uv run --env-file .env pytest peer/hid_logic
uv run --env-file .env pytest peer/hid_keyboard
```

Test results are saved between runs (`--save-state` is enabled by default). Re-running only failed tests is therefore possible with:

```sh
uv run --env-file .env pytest --lf
```

To generate an HTML report, uncomment `PYTEST_ADDOPTS` in `.env` or pass the options directly:

```sh
uv run --env-file .env pytest --html=report.html --self-contained-html
```

## Test directories

### `peer/` — Two-board tests

Uses two ESP32-S3 boards: one runs EspUsbHost as the USB host, the other runs an Arduino USB device sketch as the peer. The boards communicate over USB.

See [peer/README.md](peer/README.md) for hardware wiring and coverage details.

### `loopback/` — Single-board tests (work in progress)

Uses an ESP32-P4 that runs both USB host and USB device on the same chip. No second board is needed. This directory is currently being organized.

## pytest-embedded-arduino-cli

[pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli) is the plugin that connects pytest-embedded with Arduino CLI. It builds and flashes sketches automatically before each test run.

### How serial ports are resolved

The plugin resolves the serial port for each board in this order:

1. `--port` CLI option
2. `TEST_SERIAL_PORT_<PROFILE>` environment variable, where `<PROFILE>` is the sketch.yaml profile name **uppercased with hyphens replaced by underscores**
3. `TEST_SERIAL_PORT` environment variable (fallback for any profile)

For example, a profile named `s3_peer_host` in `sketch.yaml` maps to `TEST_SERIAL_PORT_S3_PEER_HOST` in `.env`.

### sketch.yaml and profiles

Each test sketch has a `sketch.yaml` that declares board profiles:

```yaml
profiles:
  s3_peer_host:
    fqbn: esp32:esp32:esp32s3
default_profile: s3_peer_host
```

Peer board sketches live in a `peer_<name>/` subdirectory alongside the main sketch, each with their own `sketch.yaml`.

### Run modes

```sh
# Build, flash, and test (default)
uv run --env-file .env pytest peer/hid_keyboard

# Build only — no board needed
uv run --env-file .env pytest peer/hid_keyboard --run-mode=build

# Test only — skip build and flash, use already-flashed firmware
uv run --env-file .env pytest peer/hid_keyboard --run-mode=test
```

### Verbose output

```sh
# Show compile and upload commands
uv run --env-file .env pytest -v

# Show full execution context (sketch dir, profile, port, etc.)
uv run --env-file .env pytest -vv
```

### Arduino CLI setup

Arduino CLI must be in `PATH` with the required board cores installed. Refresh the package index periodically:

```sh
arduino-cli core update-index
arduino-cli lib update-index
```

## Dependencies

Python dependencies are declared in `pyproject.toml` and locked in `uv.lock`. `uv run` installs them automatically into a local virtual environment on first use.

Key packages:

| Package | Role |
|---------|------|
| `pytest` | Test runner |
| `pytest-embedded` | Embedded device test framework |
| `pytest-embedded-serial` | Serial communication with boards |
| `pytest-embedded-arduino-cli` | Build and flash via Arduino CLI |
| `pytest-html` | Optional HTML report generation |
