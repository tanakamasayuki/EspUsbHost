# Manual Tests

> 日本語版: [README.ja.md](README.ja.md)

This directory contains tests that cannot be automated.
Each test is manual because the environment cannot be fully controlled by software — not because automation was inconvenient.

See [../TEST_PLAN.md](../TEST_PLAN.md) for the overall test strategy and the rationale for each manual test category.

## Running a manual test

Manual test files do **not** use the `test_` prefix, so pytest does not collect them automatically.
Run explicitly after preparing the required hardware:

```sh
cd tests
uv run --env-file .env pytest manual/smoke/smoke.py -v -s
uv run --env-file .env pytest manual/vcp_ftdi/vcp_ftdi.py -v -s
```

Always pass `-s` for manual tests so that serial output and operator prompts are visible.

The default board profile is `esp32s3`. To use a different board, pass `--profile`:

```sh
uv run --env-file .env pytest manual/smoke/smoke.py -v -s --profile esp32p4
```

Available profiles are defined in each test's `sketch.yaml`.

## Test catalog

| Test | Description | Hardware required | Status |
|------|-------------|-------------------|--------|
| [`smoke/`](smoke/) | Procedure check — verifies that the manual test workflow itself works (build, flash, serial, operator prompt). Not a feature test. Run this first on a new machine. | ESP32-S3 or ESP32-P4 | ✅ |
| [`vcp_ftdi/`](vcp_ftdi/) | TX/RX loopback via FTDI VCP (VID 0x0403) | FTDI device (FT232R etc.) with TX/RX shorted | ✅ |
| [`vcp_cp210x/`](vcp_cp210x/) | TX/RX loopback via CP210x VCP (VID 0x10C4) | CP210x device (CP2102 etc.) with TX/RX shorted | ✅ |
| [`vcp_ch34x/`](vcp_ch34x/) | TX/RX loopback via CH34x VCP (VID 0x1A86) | CH34x device (CH340 etc.) with TX/RX shorted | ✅ |
| [`vcp_pl2303/`](vcp_pl2303/) | TX/RX loopback via PL2303 VCP (VID 0x067B PID 0x2303) | PL2303 device with TX/RX shorted | ✅ |
| [`vcp_pl2303gs/`](vcp_pl2303gs/) | TX/RX loopback via PL2303GS VCP (VID 0x067B PID 0x23A3) | PL2303GS device with TX/RX shorted | ✅ |
| [`esp32_autoreset/`](esp32_autoreset/) | DTR/RTS reset and ROM download-mode check with a target ESP32 | USB serial adapter wired to a target ESP32 auto-reset circuit | ✅ |
| [`keyboard_leds/`](keyboard_leds/) | NumLock/CapsLock LED visual confirmation | USB keyboard with indicator LEDs | ✅ |
| [`multi_hid_keyboard_mouse/`](multi_hid_keyboard_mouse/) | Keyboard and mouse deliver events independently when connected simultaneously | USB keyboard + USB mouse | ✅ |
| [`multi_serial/`](multi_serial/) | Two serial devices work independently via `setAddress()` | Two USB serial devices with TX/RX shorted | ✅ |
| [`hotplug/`](hotplug/) | Connect/disconnect events and no crash after repeated cycles | Any USB device | ✅ |
| [`hub_info/`](hub_info/) | Displays hub topology info for devices connected through a USB hub | USB hub + two USB devices | ✅ |
| `hub_power/` | Per-port power control — turn hub ports on/off and verify devices connect/disconnect accordingly | USB hub with per-port power switching | 📋 planned |

## ESP32-S3 HCD Channel Limits

ESP32-S3 has 8 USB host channels (`OTG_NUM_HOST_CHAN`). When several devices are connected through a USB hub, ESP-IDF may fail to allocate enough host channels for all claimed interfaces. Because the exact channel use depends on the hub and device descriptors, this document records only combinations that have been observed with this test suite.

Observed `multi_serial` results on ESP32-S3 through a USB hub:

| Combination | Result | Notes |
|-------------|--------|-------|
| FTDI + CP210x | PASS | Both loopback ports passed |
| FTDI + CH34x | FAIL | Endpoint allocation failed with HCD channel exhaustion |

If the log contains `No more HCD channels available`, `EP Alloc error: ESP_ERR_NOT_SUPPORTED`, or `Claiming interface error: ESP_ERR_NOT_SUPPORTED`, the selected device mix exceeded the available ESP32-S3 host-channel resources in that setup. Test one serial device at a time or use a different hardware setup.

## Test results

Manual test results are saved automatically by `--save-state` to:

```
tests/.pytest-results/state.json
```

This file records the last outcome and timestamp for each test node ID. When a feature is changed, check this file to see when the related manual tests were last run and whether they need to be re-run. Tests that have never passed, or that passed before the relevant code was modified, should be flagged for re-execution.

Note that this file is local to the machine and not committed to the repository. It may be missing (deleted or never run), and results from other machines are not reflected. Treat it as a helpful hint, not a definitive record.

## Why each category is manual

| Category | Reason automation is not possible |
|----------|------------------------------------|
| VCP serial (FTDI, CP210x, CH34x) | Requires real VCP hardware; ESP32 cannot emulate these vendor-specific protocols |
| Multiple simultaneous devices | Requires multiple physical USB devices connected at the same time |
| Keyboard LED visual verification | Pass/fail depends on whether a physical LED lights up |
| Device hot-plug stress | Requires a person to physically plug and unplug cables on a timing cue |
| USB hub (info display, power management) | Requires a physical USB hub. While it is technically possible to route multiple devices through a hub in the automated test environment, doing so would mix hub behaviour into the test results and introduce noise. Automated tests therefore use direct 1-to-1 connections only |
| Hub cascade (hub behind hub) | Requires two or more nested physical hubs; cannot be emulated in software |
| Human-only observable output (audio, MIDI, etc.) | Involves physical output such as sound that cannot be observed directly from software. Automatable with audio loopback hardware, but typically requires human confirmation |

## Judgment approach

Tests should use the simplest judgment method that works for the scenario, in order of preference:

1. **Special device, fully automated** — connect the required hardware before running; the test itself runs fully automatically via `dut.expect()`. Manual only because the hardware cannot be emulated.

2. **Timeout** — the test prints an instruction (e.g., "connect the device now"), then waits with a timeout for the device to be recognised. No y/n input needed.

3. **Human visual confirmation** — reserved for things that cannot be observed in software (e.g., a physical LED, audio output). The test asks the operator to judge and enter `y` / `n`.

## Test independence

Flashing is done once per `.py` file. Multiple test functions within the same file share the device state — the board is not reset between them, so a later test may be affected by what an earlier test left behind.

For full independence, separate the test into its own `.py` file with its own sketch. The recommended practice is **one test function per `.py` file**. Only group multiple tests in one file when they intentionally share setup state and you accept that dependency.

## Test file template

```python
"""
Purpose:
    One sentence describing what this test verifies.

Why manual:
    The specific reason this test cannot be automated.
    (e.g., "Requires real FTDI hardware — ESP32 cannot emulate this VID/PID.")

Required hardware:
    - Device type and model (e.g., FT232R breakout board)
    - Connection method (e.g., USB-A to the ESP32-S3 host board)

Setup:
    1. Flash the host board with the EspUsbHostUSBSerial sketch.
    2. Connect the VCP device to the USB port of the host board.
    3. Run: uv run --env-file .env pytest manual/<name>/<name>.py -v -s
"""

import pytest

# ---------------------------------------------------------------------------
# Test(s)
# ---------------------------------------------------------------------------

def test_something(dut):
    """
    Expected result (pass):  <concrete observable outcome>
    Expected result (fail):  <what failure looks like>
    """
    ...
```

### Writing expected results

Write expected results so that any team member — not just the author — can judge pass or fail without ambiguity.

| Avoid | Write instead |
|-------|---------------|
| "Check if the LED lights up" | "NumLock LED turns ON within 1 second of sending `n`; LED turns OFF after sending `0`" |
| "Verify data is received" | "All 64 bytes sent from the host appear in the device's serial output in the same order" |
| "Make sure it doesn't crash" | "After 10 connect/disconnect cycles the sketch is still printing to Serial and no error is logged" |
