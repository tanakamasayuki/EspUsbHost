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
uv run --env-file .env pytest manual/vcp_serial/vcp_serial.py -v -s
uv run --env-file .env pytest manual/keyboard_leds/keyboard_leds.py -v -s
```

Always pass `-s` for manual tests so that serial output and operator prompts are visible.

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
