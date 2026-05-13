"""
Purpose:
    Verify that EspUsbHost fires connect/disconnect events correctly and does
    not crash after repeated plug/unplug cycles.

Why manual:
    Requires a person to physically plug and unplug a USB device on a timing cue.

Required hardware:
    - ESP32-S3 host board
    - Any USB device

Setup:
    1. Connect the host board to the PC (do NOT connect the test device yet).
    2. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    3. Run: uv run --env-file .env pytest manual/hotplug/hotplug.py -v -s
"""

import pytest


def test_hotplug(dut):
    """
    Expected result (pass):  Sketch counts 5 connect/disconnect cycles and
                             prints "[PASS]" with no error output.
    Expected result (fail):  Fewer than 5 cycles counted, crash, or timeout.
    """
    dut.expect("Plug and unplug a USB device 5 times")
    print("\nPlug and unplug a USB device 5 times.")
    dut.expect(r"\[PASS\]", timeout=120)
