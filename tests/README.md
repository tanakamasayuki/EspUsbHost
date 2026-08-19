# Tests

> 日本語版: [README.ja.md](README.ja.md)

This directory contains automated and manual tests for EspUsbHost.
For the overall test strategy and coverage matrix, see [TEST_PLAN.md](TEST_PLAN.md). Tests run on real ESP32 hardware via [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) with the Arduino CLI backend.

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
# Profile-specific serial ports.
# These names match sketch.yaml profile names uppercased by pytest-embedded.
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyUSB0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB1
TEST_SERIAL_PORT_ESP32S3=/dev/ttyACM0
TEST_SERIAL_PORT_ESP32P4=/dev/ttyACM1

# pytest options to generate an HTML report
#PYTEST_ADDOPTS="--html=report.html --self-contained-html"
```

Set each `TEST_SERIAL_PORT_*` variable to the actual serial port for the corresponding board.

Profile names describe the connection role. `peer/` uses always-connected
dedicated boards, so it uses dedicated profiles such as `s3_peer_host` and
`s3_peer_device`. `examples/`, `manual/`, and `probe/` are intended for
individually run generic boards, so they use `esp32s3` and `esp32p4`.
`loopback/` currently only contains README files and has no runnable profile in
this repository.

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

Builds are cached per sketch directory, and that cache is not invalidated by
everything that should invalidate it. Pass `--clean` (it forwards `--clean` to
`arduino-cli compile` and removes stale artifacts first) whenever the build
inputs changed rather than the sources:

```sh
uv run --env-file .env pytest --clean peer/usb_ncm_throughput
```

Required after changing compiler flags (a sketch's `build_opt.h`, e.g. the peer
in `peer/usb_ncm_throughput`), changing a profile in `sketch.yaml`, or upgrading
the arduino-esp32 core. Without it a run can silently use the previous binary:
a `build_opt.h` change is picked up for the sketch itself while the cached
library is reused, so the firmware ends up built from two different
configurations and the test measures something other than what it claims to.

Test results are saved between runs (`--save-state` is enabled by default). Re-running only failed tests is therefore possible with:

```sh
uv run --env-file .env pytest --lf
```

To generate an HTML report, uncomment `PYTEST_ADDOPTS` in `.env` or pass the options directly:

```sh
uv run --env-file .env pytest --html=report.html --self-contained-html
```

After each test, the host `dut.log` and peer `peer-*.log` files are audited
automatically. Suspicious ESP-IDF errors, `ESP_ERR_*` values, panics, asserts,
and watchdog messages are summarized under `serial log audit` without failing
the test. When the HTML report is enabled, findings are also appended to that
test's expandable log. Complete serial logs remain available under
`/tmp/pytest-embedded/`.

A few messages are produced by healthy runs; those are listed in
`_KNOWN_SERIAL_FINDINGS` in `conftest.py` and reported as `KNOWN:` with a reason
instead of as unexpected findings. Each entry is pinned to the test and log it
was observed in and capped at a maximum count, so the same message appearing in
a different test, or more often than expected, is still reported.

## Test directories

### `peer/` — Two-board tests

Uses two ESP32-S3 boards: one runs EspUsbHost as the USB host, the other runs a
sketch based on the Arduino-ESP32 standard USB Device implementation. The boards
communicate over USB.

This directory mainly checks interoperability with the Arduino Core standard
Device implementation. Detailed combination tests with the sibling
`EspUsbDevice` library and ESP32-P4 loopback coverage are handled primarily in
the EspUsbDevice repository.

See [peer/README.md](peer/README.md) for hardware wiring and coverage details.

### `loopback/` — Single-board tests (work in progress)

Reserved for tests that run both USB host and USB device on one ESP32-P4. There
are currently no runnable loopback tests in this repository; practical loopback
coverage is currently developed mainly in EspUsbDevice.

### `manual/` — Manual tests

Tests that cannot be automated because the environment cannot be fully
controlled by software: they need special hardware or a human judging the
result. pytest still builds and flashes the sketch and prompts the operator.
See [manual/README.md](manual/README.md) for the test catalog and the known
hardware issues collected there.

### `probe/` — Bring-up probes

Sketches for ESP32-P4 USB port identification, HS/FS Host checks, HS Device checks, and Hardware CDC/JTAG checks. They depend on board wiring and host-PC enumeration, so they are not formal regression tests. See [probe/README.md](probe/README.md) for details.

### `unit/` — Host-side unit tests

Pure C++ / data-conversion tests that run on the host with g++. No board or
serial port is required. See [unit/README.md](unit/README.md) for what each
test extracts from the library sources and covers.

## pytest-embedded-arduino-cli

[pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli) is the plugin that connects pytest-embedded with Arduino CLI. It builds and flashes sketches automatically before each test run.

### How serial ports are resolved

The plugin resolves the serial port for each board in this order:

1. `--port` CLI option
2. `TEST_SERIAL_PORT_<PROFILE>` environment variable, where `<PROFILE>` is the sketch.yaml profile name **uppercased with hyphens replaced by underscores**
3. `TEST_SERIAL_PORT` environment variable (fallback for any profile)

For example, a profile named `s3_peer_host` in `sketch.yaml` maps to `TEST_SERIAL_PORT_S3_PEER_HOST` in `.env`.

For always-connected test setups, configure dedicated ports such as
`TEST_SERIAL_PORT_S3_PEER_HOST` and
`TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE`. For individually run examples,
manual tests, and probes, use `TEST_SERIAL_PORT_ESP32S3` and
`TEST_SERIAL_PORT_ESP32P4`.

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
